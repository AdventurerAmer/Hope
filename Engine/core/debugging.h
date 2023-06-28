#pragma once

#include "core/logging.h"

struct Debug_State
{
    Logger main_logger;
};

extern Debug_State global_debug_state;

#define HE_LOGGING 1
#if HE_LOGGING

#define DebugPrintf(channel, verbosity, format, ...) debug_printf(\
&global_debug_state.main_logger,\
Glue(Channel_, channel),\
Glue(Verbosity_, verbosity),\
"[" Stringify(channel) "-" Stringify(verbosity) "]: " format,\
__VA_ARGS__)

#else

#define DebugPrintf(channel, verbosity, format, ...)

#endif