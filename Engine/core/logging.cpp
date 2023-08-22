#include <stdio.h>
#include <stdarg.h>

#include "logging.h"
#include "platform.h"
#include "memory.h"
#include "containers/string.h"

static const char *channel_to_ansi_string[] =
{
#define X(name, str) str,
    Channel_Table
#undef X
};

bool init_logger(Logger *logger, const char *name, Verbosity verbosity, U64 channel_mask, Memory_Arena *arena)
{
    Temprary_Memory_Arena temprary_arena;
    begin_temprary_memory_arena(&temprary_arena, arena);

    bool result = true;

    logger->verbosity = verbosity;
    logger->channel_mask = channel_mask;

    Logging_Channel *main_channel = &logger->main_channel;
    main_channel->name = name;

    // note(amer): should logging files be in the bin folder or a separate folder for logs ?
    String main_channel_path = format_string(temprary_arena.arena, "logging/%s.log", name);

    main_channel->log_file_result = platform_open_file(main_channel_path.data, OpenFileFlag_Write);
    if (!main_channel->log_file_result.success)
    {
        result = false;
    }

    for (U32 channel_index = 0;
         channel_index < Channel_Count;
         channel_index++)
    {
        Logging_Channel *channel = &logger->channels[channel_index];
        channel->name = channel_to_ansi_string[channel_index];

        String channel_path = format_string(temprary_arena.arena, "logging/%s.log", channel->name);

        channel->log_file_result = platform_open_file(channel_path.data, OpenFileFlag_Write);
        if (!channel->log_file_result.success)
        {
            result = false;
            break;
        }
    }

    end_temprary_memory_arena(&temprary_arena);
    return result;
}

void deinit_logger(Logger *logger)
{
    platform_close_file(&logger->main_channel.log_file_result);

    for (U32 channel_index = 0;
         channel_index < Channel_Count;
         channel_index++)
    {
        Logging_Channel *channel = &logger->channels[channel_index];
        platform_close_file(&channel->log_file_result);
    }
}

void set_verbosity(Logger *logger, Verbosity verbosity)
{
    logger->verbosity = verbosity;
}

void enable_channel(Logger *logger, Channel channel)
{
    U64 mask = U64(1) << (U64)channel;
    logger->channel_mask |= mask;
}

void enable_all_channels(Logger *logger)
{
    logger->channel_mask = 0xFFFFFFFFFFFFFFFF;
}

void disable_channel(Logger *logger, Channel channel)
{
    U64 mask = U64(1) << (U64)channel;
    logger->channel_mask &= ~mask;
}

void disable_all_channels(Logger *logger)
{
    logger->channel_mask = 0;
}

void debug_printf(Logger *logger, Channel channel, Verbosity verbosity, Memory_Arena *arena, const char *format, ...)
{
    Temprary_Memory_Arena temprary_arena;
    begin_temprary_memory_arena(&temprary_arena, arena);

    va_list args;
    va_start(args, format);

    String message = format_string(arena, format, args);

    Logging_Channel *main_channel = &logger->main_channel;

    if (platform_write_data_to_file(&main_channel->log_file_result,
                                    main_channel->log_file_offset,
                                    (void*)message.data, message.count))
    {
        main_channel->log_file_offset += message.count;
    }

    Logging_Channel *logging_channel = &logger->channels[U8(channel)];
    if (platform_write_data_to_file(&logging_channel->log_file_result,
                                    logging_channel->log_file_offset,
                                    (void*)message.data, message.count))
    {
        logging_channel->log_file_offset += message.count;
    }

    if ((logger->channel_mask & (U64(1) << channel)) && logger->verbosity >= verbosity)
    {
        platform_debug_printf(message.data);
    }

    end_temprary_memory_arena(&temprary_arena);
    va_end(args);
}