#include "job_system.h"

#include "engine.h"
#include "platform.h"

#include "containers/queue.h"

#include <atomic>

#define JOB_COUNT_PER_THREAD 256

struct Thread_State
{
    Semaphore job_queue_semaphore;
    Mutex job_queue_mutex;
    Ring_Queue< Job > job_queue;

    Job_Flag job_flags;
    Thread thread;
};

struct Job_System_State
{
    std::atomic< bool > running;
    std::atomic< U32 > in_progress_job_count;

    U32 thread_count;
    Thread_State *thread_states;
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
        platform_lock_mutex(job_queue_mutex);

        if (!job_system_state.running && count(job_queue) == 0)
        {
            platform_unlock_mutex(job_queue_mutex);
            break;
        }

        Job job = {};
        bool popped = pop(job_queue, &job);
        platform_unlock_mutex(job_queue_mutex);
        
        if (popped)
        {
            HOPE_Assert(job.proc);
            Job_Result result = job.proc(job.parameters);
            if (job.completed_proc)
            {
                job.completed_proc(result);
            }
            job_system_state.in_progress_job_count.fetch_sub(1);
        }
        else
        {
            wait_for_semaphore(job_queue_semaphore);
        }
    }

    return 0;
}

bool init_job_system(Engine *engine)
{
    Memory_Arena *arena = &engine->memory.permanent_arena;

    U32 thread_count = platform_get_thread_count();
    HOPE_Assert(thread_count);

    job_system_state.running.store(true);
    job_system_state.in_progress_job_count.store(0);
    job_system_state.thread_count = thread_count;
    job_system_state.thread_states = AllocateArray(arena, Thread_State, thread_count);

    for (U32 thread_index = 0; thread_index < thread_count; thread_index++)
    {
        Thread_State *thread_state = &job_system_state.thread_states[thread_index];

        bool job_queue_semaphore_created = platform_create_semaphore(&thread_state->job_queue_semaphore, 0, "WorkerSemaphore");
        HOPE_Assert(job_queue_semaphore_created);

        bool job_queue_mutex_created = platform_create_mutex(&thread_state->job_queue_mutex);
        HOPE_Assert(job_queue_mutex_created);

        thread_state->job_flags = JobFlag_GeneralPurpose;

        // note(amer): using one thread for loading files for now...
        if (thread_index == thread_count - 1)
        {
            thread_state->job_flags = Job_Flag(thread_state->job_flags|JobFlag_Loading);
        }

        init_ring_queue(&thread_state->job_queue, JOB_COUNT_PER_THREAD, arena);
        bool thread_created_and_started = platform_create_and_start_thread(&thread_state->thread, execute_thread_work, thread_state, "WorkerThread");
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
    Thread_State *least_worked_thread_state = nullptr;
    U32 least_work_count_so_far = HOPE_MAX_U32;

    for (U32 thread_index = 0; thread_index < job_system_state.thread_count; thread_index++)
    {
        Thread_State *thread_state = &job_system_state.thread_states[thread_index];
        if ((thread_state->job_flags & flags) == flags)
        {
            platform_lock_mutex(&thread_state->job_queue_mutex);
            U32 job_queue_in_queue = count(&thread_state->job_queue);
            if (job_queue_in_queue < least_work_count_so_far)
            {
                least_worked_thread_state = thread_state;
                least_work_count_so_far = job_queue_in_queue;
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

    signal_semaphore(&least_worked_thread_state->job_queue_semaphore);
}

void wait_for_all_jobs_to_finish()
{
    while (job_system_state.in_progress_job_count.load())
    {
        // todo(amer): do useful work here...
    }
}