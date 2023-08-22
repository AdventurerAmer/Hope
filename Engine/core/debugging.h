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

#define HOPE_DebugPrintf(channel, verbosity, format, ...) debug_printf(\
&global_debug_state.main_logger,\
HOPE_Glue(Channel_, channel),\
HOPE_Glue(Verbosity_, verbosity),\
&global_debug_state.arena,\
"[" HOPE_Stringify(channel) "-" HOPE_Stringify(verbosity) "]: " format,\
__VA_ARGS__)

#else

#define HOPE_DebugPrintf(channel, verbosity, format, ...)

#endif