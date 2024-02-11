#pragma once

#include "defines.h"
#include "containers/array_view.h"
#include "containers/dynamic_array.h"
#include "containers/resource_pool.h"

enum class Job_Result : U8
{
    FAILED,
    ABORTED,
    SUCCEEDED
};

typedef void (*Job_Completed_Proc)(Job_Result result);

struct Job_Parameters
{
    struct Memory_Arena *arena;
    void *data;
    U64 size;
    U16 alignment = 1;
};

typedef Job_Result (*Job_Proc)(const Job_Parameters &params);

struct Job_Data
{
    Job_Parameters     parameters;
    Job_Proc           proc;
    Job_Completed_Proc completed_proc;
};

struct Job_Ref
{
    S32 index;
    U32 generation;
};

struct Job
{
    Job_Data      data;
    volatile bool finished;

    volatile U32             remaining_job_count;
    Mutex                    dependent_jobs_mutex;
    Dynamic_Array< Job_Ref > dependent_jobs;
};

using Job_Handle = Resource_Handle< Job >;

bool init_job_system();
void deinit_job_system();

Job_Handle execute_job(Job_Data job_data, Array_View< Job_Handle > wait_for_jobs = { 0, nullptr });

void wait_for_all_jobs_to_finish();