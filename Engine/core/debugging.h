#pragma once

#include "core/logging.h"
#include "memory.h"

struct Debug_State
{
    Logger main_logger;
    Memory_Arena arena;
};

extern Debug_State global_debug_state;

#define HE_LOGGING 1
#if HE_LOGGING

#define HE_LOG(channel, verbosity, format, ...)\
    log(&global_debug_state.main_logger,\
        HE_GLUE(Channel_, channel),\
        HE_GLUE(Verbosity_, verbosity),\
        &global_debug_state.arena,\
        "[" HE_STRINGIFY(channel) "-" HE_STRINGIFY(verbosity) "]: " format,\
        __VA_ARGS__)

#else

#define HE_LOG(channel, verbosity, format, ...)

#endif