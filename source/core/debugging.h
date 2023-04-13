#pragma once

#include "core/logging.h"

struct Debug_State
{
    Logger main_logger;
};

#define HE_LOGGING 1
#if HE_LOGGING

#define HE_DebugPrintf(channel, verbosity, format, ...) debug_printf(\
&debug_state.main_logger,\
HE_Glue(Channel_, channel),\
HE_Glue(Verbosity_, verbosity),\
"[" HE_Stringify(channel) "-" HE_Stringify(verbosity) "]: " format,\
__VA_ARGS__)

#else

#define HE_DebugPrintf(channel, verbosity, format, ...)

#endif