#pragma once

#include "logging.h"
#include "memory.h"

struct Engine_Configuration
{
    Mem_Size permanent_memory_size;
    Mem_Size transient_memory_size;
};

struct Engine
{
    Mem_Size permanent_memory_size;
    void *permenent_memory;

    Mem_Size transient_memory_size;
    void *transient_memory;

    Memory_Arena permanent_arena;
    Memory_Arena transient_arena;
};

internal_function bool
startup(Engine *engine, const Engine_Configuration &configuration);

internal_function void
shutdown(Engine *engine);