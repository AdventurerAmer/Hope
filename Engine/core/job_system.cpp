#include "job_system.h"

#include "engine.h"
#include "platform.h"
#include "memory.h"
#include "debugging.h"
#include "containers/queue.h"

#include <atomic>

#define JOB_COUNT_PER_THREAD 256

struct Thread_State
{
    Semaphore job_queue_semaphore;
    Mutex job_queue_mutex;
    Ring_Queue< Job > job_queue;
    Memory_Arena arena;
    Job_Flag job_flags;
    U32 thread_index;
    Thread thread;
};

struct Job_System_State
{
    std::atomic< bool > running;
    std::atomic< U32 > in_progress_job_count;

    Memory_Arena arena;

    U32 thread_count;
    Thread_State *thread_states;

    Free_List_Allocator job_data_allocator;
};

static Job_System_State job_system_state;

unsigned long execute_thread_work(void *params)
{
    Thread_State *thread_state = (Thread_State *)params;
    Semaphore *job_queue_semaphore = &thread_state->job_queue_semaphore;
    Mutex *job_queue_mutex = &thread_state->job_queue_mutex;
    Ring_Queue< Job > *job_queue = &thread_state->job_queue;

    while (true)
    {
        bool signaled = wait_for_semaphore(job_queue_semaphore);
        HOPE_Assert(signaled);

        platform_lock_mutex(job_queue_mutex);

        if (!job_system_state.running && count(job_queue) == 0)
        {
            platform_unlock_mutex(job_queue_mutex);
            break;
        }

        Job job = {};
        bool peeked = peek(job_queue, &job);
        platform_unlock_mutex(job_queue_mutex);
        HOPE_Assert(peeked);
        HOPE_Assert(job.proc);

        Temprary_Memory_Arena temprary_memory_arena = {};
        begin_temprary_memory_arena(&temprary_memory_arena, &thread_state->arena);
        job.parameters.temprary_memory_arena = &temprary_memory_arena;

        Job_Result result = job.proc(job.parameters);
        if (job.completed_proc)
        {
            job.completed_proc(result);
        }

        end_temprary_memory_arena(&temprary_memory_arena);
        pop(job_queue);
        job_system_state.in_progress_job_count.fetch_sub(1);

        deallocate(&job_system_state.job_data_allocator, job.parameters.data);
    }

    return 0;
}

bool init_job_system(Engine *engine)
{
    Memory_Arena *arena = &engine->memory.permanent_arena;

    init_free_list_allocator(&job_system_state.job_data_allocator, arena, HOPE_MegaBytes(32));
    job_system_state.arena = create_sub_arena(arena, HOPE_MegaBytes(32));

    U32 thread_count = platform_get_thread_count();
    HOPE_Assert(thread_count);

    job_system_state.running.store(true);
    job_system_state.in_progress_job_count.store(0);
    job_system_state.thread_count = thread_count;
    job_system_state.thread_states = AllocateArray(arena, Thread_State, thread_count);

    for (U32 thread_index = 0; thread_index < thread_count; thread_index++)
    {
        Thread_State *thread_state = &job_system_state.thread_states[thread_index];
        thread_state->thread_index = thread_index;

        thread_state->arena = create_sub_arena(arena, HOPE_MegaBytes(32));

        init_ring_queue(&thread_state->job_queue, JOB_COUNT_PER_THREAD, arena);

        bool job_queue_semaphore_created = platform_create_semaphore(&thread_state->job_queue_semaphore);
        HOPE_Assert(job_queue_semaphore_created);

        bool job_queue_mutex_created = platform_create_mutex(&thread_state->job_queue_mutex);
        HOPE_Assert(job_queue_mutex_created);

        thread_state->job_flags = JobFlag_GeneralPurpose;

        bool thread_created_and_started = platform_create_and_start_thread(&thread_state->thread, execute_thread_work, thread_state, "HopeWorkerThread");
        HOPE_Assert(thread_created_and_started);
    }

    return true;
}

void deinit_job_system()
{
    wait_for_all_jobs_to_finish();
    job_system_state.running.store(false);
}

void execute_job(Job job, Job_Flag flags)
{
    if (job.parameters.data)
    {
        void *data = allocate(&job_system_state.job_data_allocator, job.parameters.size, 0);
        copy_memory(data, job.parameters.data, job.parameters.size);
        job.parameters.data = data;
    }

    Thread_State *least_worked_thread_state = nullptr;
    U32 least_work_count_so_far = HOPE_MAX_U32;

    for (U32 thread_index = 0; thread_index < job_system_state.thread_count; thread_index++)
    {
        Thread_State *thread_state = &job_system_state.thread_states[thread_index];
        if ((thread_state->job_flags & flags) == flags)
        {
            platform_lock_mutex(&thread_state->job_queue_mutex);
            U32 job_count_in_queue = count(&thread_state->job_queue);
            if (job_count_in_queue < least_work_count_so_far)
            {
                least_worked_thread_state = thread_state;
                least_work_count_so_far = job_count_in_queue;
            }
            platform_unlock_mutex(&thread_state->job_queue_mutex);
        }
    }

    HOPE_Assert(least_worked_thread_state);
    job_system_state.in_progress_job_count.fetch_add(1);

    platform_lock_mutex(&least_worked_thread_state->job_queue_mutex);
    bool pushed = push(&least_worked_thread_state->job_queue, job);
    HOPE_Assert(pushed);
    platform_unlock_mutex(&least_worked_thread_state->job_queue_mutex);

    bool signaled = signal_semaphore(&least_worked_thread_state->job_queue_semaphore);
    HOPE_Assert(signaled);
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

        // we don't actually wait here we just decrement the semaphore counter
        wait_for_semaphore(&most_worked_thread_state->job_queue_semaphore);

        // todo(amer): manual poping from the back
        Ring_Queue< Job > *job_queue = &most_worked_thread_state->job_queue;
        HOPE_Assert(!empty(job_queue));
        Job job = job_queue->data[(job_queue->write & job_queue->mask) - 1];
        job_queue->write--;

        platform_unlock_mutex(&most_worked_thread_state->job_queue_mutex);
        HOPE_Assert(job.proc);

        Temprary_Memory_Arena temprary_memory_arena = {};
        begin_temprary_memory_arena(&temprary_memory_arena, &job_system_state.arena);
        job.parameters.temprary_memory_arena = &temprary_memory_arena;

        Job_Result result = job.proc(job.parameters);
        if (job.completed_proc)
        {
            job.completed_proc(result);
        }
        end_temprary_memory_arena(&temprary_memory_arena);

        deallocate(&job_system_state.job_data_allocator, job.parameters.data);
        job_system_state.in_progress_job_count.fetch_sub(1);
    }
}