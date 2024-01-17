#pragma once

#include "defines.h"
#include "platform.h"

#define HE_LOGGING 1
#if HE_LOGGING

#define HE_LOG(channel, verbosity, format, ...)\
    log(HE_GLUE(Channel_, channel),\
        HE_GLUE(Verbosity_, verbosity),\
        "[" HE_STRINGIFY(channel) "-" HE_STRINGIFY(verbosity) "]: " format,\
        __VA_ARGS__)
#else

#define HE_LOG(channel, verbosity, format, ...)

#endif

#define Verbosity_Table\
    X(Fetal, "fetal")\
    X(Error, "error")\
    X(Warn, "warn")\
    X(Info, "info")\
    X(Log, "log")\
    X(Debug, "debug")\
    X(Trace, "trace")\

enum Verbosity : U8
{
#define X(name, str) Verbosity_##name,
    Verbosity_Table
#undef X
    Verbosity_Count
};

#define Channel_Table\
    X(Core, "core")\
    X(Resource, "resource")\
    X(Gameplay, "gameplay")\
    X(Physics, "physics")\
    X(Rendering, "rendering")\
    X(Audio, "audio")\

enum Channel : U8
{
#define X(name, str) Channel_##name,
    Channel_Table
#undef X
    Channel_Count
};

struct Logging_Channel
{
    const char *name;

    U64 log_file_offset;
    Open_File_Result log_file_result;
};

struct Logger
{
    Verbosity verbosity;

    static_assert(Channel_Count <= 64);
    U64 channel_mask;

    Logging_Channel main_channel;
    Logging_Channel channels[Channel_Count];
};

bool init_logging_system();
void deinit_logging_system();

bool init_logger(Logger *logger, const char *name, Verbosity verbosity, U64 channel_mask);
void deinit_logger(Logger *logger);

void set_verbosity(Logger *logger, Verbosity verbosity);

void enable_channel(Logger *logger, Channel channel);

void enable_all_channels(Logger *logger);

void disable_channel(Logger *logger, Channel channel);

void disable_all_channels(Logger *logger);

void log(Channel channel, Verbosity verobisty, const char *format, ...);