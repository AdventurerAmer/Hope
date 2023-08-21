#pragma once

#include "defines.h"
#include "platform.h"

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
    Platform_File_Handle log_file;
};

struct Logger
{
    Verbosity verbosity;

    static_assert(Channel_Count <= 64);
    U64 channel_mask;

    Logging_Channel main_channel;
    Logging_Channel channels[Channel_Count];
};

bool init_logger(Logger *logger, const char *name, Verbosity verbosity, U64 channel_mask);

void deinit_logger(Logger *logger);

void set_verbosity(Logger *logger, Verbosity verbosity);

void enable_channel(Logger *logger, Channel channel);

void enable_all_channels(Logger *logger);

void disable_channel(Logger *logger, Channel channel);

void disable_all_channels(Logger *logger);

void debug_printf(Logger *logger, Channel channel, Verbosity verobisty, const char *format, ...);