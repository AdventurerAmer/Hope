#include "job_system.h"

#include "engine.h"
#include "platform.h"
#include "memory.h"
#include "logging.h"
#include "file_system.h"

#include "containers/queue.h"

#include <atomic>

#define JOB_COUNT_PER_THREAD 1024

struct Thread_State
{
    Memory_Arena *transient_arena;
    U32 thread_index;
    Thread thread;

    Semaphore job_queue_semaphore;
    Mutex job_queue_mutex;
    Mutex dependency_mutex;

    Ring_Queue< Job_Handle > job_queue;
};

struct Job_System_State
{
    std::atomic< bool > running;
    std::atomic< U32 > in_progress_job_count;

    Free_List_Allocator job_data_allocator;

    U32 thread_count;
    Thread_State *thread_states;

    Resource_Pool< Job > job_pool;
};

static Job_System_State job_system_state;

static void schedule_job_to_least_worked_thread(Job_Handle job_handle)
{
    Thread_State *least_worked_thread_state = nullptr;
    U32 least_worked_thread_index = 0;
    U32 least_work_count_so_far = HE_MAX_U32;

    for (U32 thread_index = 0; thread_index < job_system_state.thread_count; thread_index++)
    {
        Thread_State *thread_state = &job_system_state.thread_states[thread_index];

        platform_lock_mutex(&thread_state->job_queue_mutex);
        U32 job_count_in_queue = count(&thread_state->job_queue);
        if (job_count_in_queue < least_work_count_so_far)
        {
            least_worked_thread_index = thread_index;
            least_worked_thread_state = thread_state;
            least_work_count_so_far = job_count_in_queue;
        }
        platform_unlock_mutex(&thread_state->job_queue_mutex);
    }

    HE_ASSERT(least_worked_thread_state);

    platform_lock_mutex(&least_worked_thread_state->job_queue_mutex);
    S32 index = push(&least_worked_thread_state->job_queue, job_handle);
    HE_ASSERT(index != -1);
    platform_unlock_mutex(&least_worked_thread_state->job_queue_mutex);

    bool signaled = signal_semaphore(&least_worked_thread_state->job_queue_semaphore);
    HE_ASSERT(signaled);
}

static void terminate_job(Job_Handle job_handle)
{
    job_system_state.in_progress_job_count.fetch_sub(1);

    Job *job = get(&job_system_state.job_pool, job_handle);

    for (Job_Ref dependent_job_ref : job->dependent_jobs)
    {
        Job_Handle dependent_job_handle = { .index = dependent_job_ref.index, .generation = dependent_job_ref.generation };
        if (is_valid_handle(&job_system_state.job_pool, dependent_job_handle))
        {
            terminate_job(dependent_job_handle);
        }
    }

    release_handle(&job_system_state.job_pool, job_handle);
}

static void finalize_job(Job_Handle job_handle, Job_Result result)
{
    Job *job = get(&job_system_state.job_pool, job_handle);
    platform_lock_mutex(&job->dependent_jobs_mutex);

    std::atomic_store((std::atomic<bool>*)&job->finished, true);

    for (Job_Ref job_ref : job->dependent_jobs)
    {
        Job_Handle dependent_job_handle = { job_ref.index, job_ref.generation };
        if (!is_valid_handle(&job_system_state.job_pool, dependent_job_handle))
        {
            continue;
        }

        Job *dependent_job = get(&job_system_state.job_pool, dependent_job_handle);

        if (result == Job_Result::SUCCEEDED)
        {
            U32 old_value = std::atomic_fetch_sub((std::atomic<U32>*)&dependent_job->remaining_job_count, 1);
            if (old_value == 1)
            {
                schedule_job_to_least_worked_thread(dependent_job_handle);
            }
        }
        else
        {
            terminate_job(dependent_job_handle);
        }
    }

    reset(&job->dependent_jobs);

    platform_unlock_mutex(&job->dependent_jobs_mutex);

    deallocate(&job_system_state.job_data_allocator, job->data.parameters.data);

    release_handle(&job_system_state.job_pool, job_handle);
}

unsigned long execute_thread_work(void *params)
{
    Thread_State *thread_state = (Thread_State *)params;
    Semaphore *job_queue_semaphore = &thread_state->job_queue_semaphore;
    Mutex *job_queue_mutex = &thread_state->job_queue_mutex;
    Mutex *dependency_mutex = &thread_state->dependency_mutex;
    Ring_Queue< Job_Handle > *job_queue = &thread_state->job_queue;

    while (true)
    {
        bool signaled = wait_for_semaphore(job_queue_semaphore);
        HE_ASSERT(signaled);

        platform_lock_mutex(job_queue_mutex);

        if (!job_system_state.running && count(job_queue) == 0)
        {
            platform_unlock_mutex(job_queue_mutex);
            break;
        }

        U32 index = 0;
        Job_Handle job_handle = Resource_Pool<Job>::invalid_handle;
        bool peeked = peek_front(job_queue, &job_handle, &index);
        Job *job = get(&job_system_state.job_pool, job_handle);
        platform_unlock_mutex(job_queue_mutex);

        HE_ASSERT(peeked);
        HE_ASSERT(job->data.proc);

        Temprary_Memory_Arena temprary_memory_arena = begin_temprary_memory(thread_state->transient_arena);
        job->data.parameters.arena = thread_state->transient_arena;

        Job_Result result = job->data.proc(job->data.parameters);
        if (job->data.completed_proc)
        {
            job->data.completed_proc(result);
        }

        end_temprary_memory(&temprary_memory_arena);

        pop_front(job_queue);

        finalize_job(job_handle, result);

        job_system_state.in_progress_job_count.fetch_sub(1);
    }

    return 0;
}

bool init_job_system()
{
    Memory_Arena *arena = get_permenent_arena();
    bool inited = init_free_list_allocator(&job_system_state.job_data_allocator, nullptr, HE_MEGA_BYTES(32), HE_MEGA_BYTES(32));
    HE_ASSERT(inited);

    U32 thread_count = platform_get_thread_count();
    HE_ASSERT(thread_count);

    job_system_state.running.store(true);
    job_system_state.in_progress_job_count.store(0);
    job_system_state.thread_count = thread_count;
    job_system_state.thread_states = HE_ALLOCATE_ARRAY(arena, Thread_State, thread_count);

    init(&job_system_state.job_pool, thread_count * JOB_COUNT_PER_THREAD, to_allocator(&job_system_state.job_data_allocator));

    for (U32 thread_index = 0; thread_index < thread_count; thread_index++)
    {
        Thread_State *thread_state = &job_system_state.thread_states[thread_index];
        thread_state->thread_index = thread_index;

        init(&thread_state->job_queue, JOB_COUNT_PER_THREAD, to_allocator(arena));

        bool job_queue_semaphore_created = platform_create_semaphore(&thread_state->job_queue_semaphore);
        HE_ASSERT(job_queue_semaphore_created);

        bool job_queue_mutex_created = platform_create_mutex(&thread_state->job_queue_mutex);
        HE_ASSERT(job_queue_mutex_created);

        bool dependency_mutex_created = platform_create_mutex(&thread_state->dependency_mutex);
        HE_ASSERT(dependency_mutex_created);

        bool thread_created_and_started = platform_create_and_start_thread(&thread_state->thread, execute_thread_work, thread_state, "HopeWorkerThread");
        HE_ASSERT(thread_created_and_started);

        U32 thread_id = platform_get_thread_id(&thread_state->thread);
        Thread_Memory_State *memory_state = get_thread_memory_state(thread_id);

        // todo(amer): temprary HE_MEGA_BYTES(32)
        bool memory_arena_inited = init_memory_arena(&memory_state->transient_arena, HE_MEGA_BYTES(32), HE_MEGA_BYTES(32));
        HE_ASSERT(memory_arena_inited);

        thread_state->transient_arena = &memory_state->transient_arena;
    }

    return true;
}

void deinit_job_system()
{
    wait_for_all_jobs_to_finish();
    job_system_state.running.store(false);
}

static void init_job(Job *job, Job_Data job_data)
{
    job->data = job_data;

    if (job_data.parameters.data)
    {
        // todo(amer): handle alignment
        void *data = allocate(&job_system_state.job_data_allocator, job_data.parameters.size, 1);
        copy_memory(data, job_data.parameters.data, job_data.parameters.size);
        job->data.parameters.data = data;
    }

    std::atomic_store((std::atomic<bool>*)&job->finished, false);

    if (!job->dependent_jobs.data)
    {
        platform_create_mutex(&job->dependent_jobs_mutex);
        init(&job->dependent_jobs);
    }
}

Job_Handle execute_job(Job_Data job_data, Array_View< Job_Handle > wait_for_jobs)
{
    Job_Handle job_handle = aquire_handle(&job_system_state.job_pool);
    Job *job = get(&job_system_state.job_pool, job_handle);
    init_job(job, job_data);

    std::atomic_store((std::atomic<U32>*)&job->remaining_job_count, wait_for_jobs.count);

    for (Job_Handle dependent_job_handle : wait_for_jobs)
    {
        if (is_valid_handle(&job_system_state.job_pool, dependent_job_handle))
        {
            Job *dependent_job = get(&job_system_state.job_pool, dependent_job_handle);
            platform_lock_mutex(&dependent_job->dependent_jobs_mutex);

            if (std::atomic_load((std::atomic<bool>*)&dependent_job->finished) == false)
            {
                Job_Ref job_ref = { .index = job_handle.index, .generation = job_handle.generation };
                append(&dependent_job->dependent_jobs, job_ref);
            }
            else
            {
                std::atomic_fetch_sub((std::atomic<U32>*)&job->remaining_job_count, 1);
            }

            platform_unlock_mutex(&dependent_job->dependent_jobs_mutex);
        }
        else
        {
            std::atomic_fetch_sub((std::atomic<U32>*)&job->remaining_job_count, 1);
        }
    }

    if (!job->remaining_job_count)
    {
        schedule_job_to_least_worked_thread(job_handle);
    }

    job_system_state.in_progress_job_count.fetch_add(1);

    return job_handle;
}

void wait_for_all_jobs_to_finish()
{
    while (job_system_state.in_progress_job_count.load())
    {
        Thread_State *most_worked_thread_state = nullptr;
        U32 most_work_count_so_far = 0;

        for (U32 thread_index = 0; thread_index < job_system_state.thread_count; thread_index++)
        {
            Thread_State *thread_state = &job_system_state.thread_states[thread_index];
            platform_lock_mutex(&thread_state->job_queue_mutex);
            U32 job_count_in_queue = count(&thread_state->job_queue);
            if (job_count_in_queue > 1 && job_count_in_queue > most_work_count_so_far)
            {
                most_worked_thread_state = thread_state;
                most_work_count_so_far = job_count_in_queue;
            }
            platform_unlock_mutex(&thread_state->job_queue_mutex);
        }

        if (!most_worked_thread_state)
        {
            continue;
        }

        platform_lock_mutex(&most_worked_thread_state->job_queue_mutex);

        if (count(&most_worked_thread_state->job_queue) <= 1)
        {
            platform_unlock_mutex(&most_worked_thread_state->job_queue_mutex);
            continue;
        }

        // we don't actually wait here we just decrement the semaphore counter...
        wait_for_semaphore(&most_worked_thread_state->job_queue_semaphore);

        Ring_Queue< Job_Handle > *job_queue = &most_worked_thread_state->job_queue;

        Job_Handle job_handle = Resource_Pool< Job >::invalid_handle;
        bool peeked = peek_back(job_queue, &job_handle);
        pop_back(job_queue);
        Job *job = get(&job_system_state.job_pool, job_handle);
        platform_unlock_mutex(&most_worked_thread_state->job_queue_mutex);
        HE_ASSERT(job->data.proc);

        Temprary_Memory_Arena scratch_memory = begin_scratch_memory();
        job->data.parameters.arena = scratch_memory.arena;

        Job_Result result = job->data.proc(job->data.parameters);
        if (job->data.completed_proc)
        {
            job->data.completed_proc(result);
        }

        end_temprary_memory(&scratch_memory);

        finalize_job(job_handle, result);

        job_system_state.in_progress_job_count.fetch_sub(1);
    }
}

U32 get_effective_thread_count()
{
    U32 thread_count = platform_get_thread_count();
    if (thread_count > 2)
    {
        thread_count -= 2;
    }
    return thread_count;
}
