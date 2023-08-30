#pragma once

#include "defines.h"

enum Job_Flag : U64
{
    JobFlag_GeneralPurpose = 1 << 0,
    JobFlag_Loading        = 1 << 1,
};

enum class Job_Result : U8
{
    FAILED,
    ABORTED,
    SUCCEEDED
};

typedef void (*Job_Completed_Proc)(Job_Result result);

struct Job_Parameters
{
    void *data;
};

typedef Job_Result (*Job_Proc)(const Job_Parameters &params);

struct Job
{
    Job_Parameters     parameters;
    Job_Proc           proc;
    Job_Completed_Proc completed_proc;
};

bool init_job_system(struct Engine *engine);
void deinit_job_system();

void execute_job(Job job, Job_Flag flags);

void wait_for_all_jobs_to_finish();
