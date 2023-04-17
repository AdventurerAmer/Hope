#include <stdarg.h>

#include "core/logging.h"

global_variable const char *channel_to_ansi_string[] =
{
#define X(name, str) str,
    Channel_Table
#undef X
};

global_variable const char *channel_to_filename[] =
{
#define X(name, str) str##".log",
    Channel_Table
#undef X
};

internal_function bool
init_logger(Logger *logger, const char *name,
            Verbosity verbosity, U64 channel_mask)
{
    bool result = true;

    logger->verbosity = verbosity;
    logger->channel_mask = channel_mask;

    Logging_Channel *main_channel = &logger->main_channel;
    main_channel->name = name;

    main_channel->log_file = platform_open_file(name, FileOperation_Write);
    if (!platform_is_file_handle_valid(main_channel->log_file))
    {
        result = false;
    }

    for (U32 channel_index = 0;
         channel_index < Channel_Count;
         channel_index++)
    {
        Logging_Channel *channel = &logger->channels[channel_index];
        channel->name = channel_to_ansi_string[channel_index];

        const char *channel_filename = channel_to_filename[channel_index];
        channel->log_file = platform_open_file(channel_filename, FileOperation_Write);
        if (!platform_is_file_handle_valid(channel->log_file))
        {
            result = false;
            break;
        }
    }

    return result;
}

internal_function void
deinit_logger(Logger *logger)
{
    platform_close_file(logger->main_channel.log_file);

    for (U32 channel_index = 0;
         channel_index < Channel_Count;
         channel_index++)
    {
        Logging_Channel *channel = &logger->channels[channel_index];
        platform_close_file(channel->log_file);
    }
}

internal_function void
set_verbosity(Logger *logger, Verbosity verbosity)
{
    logger->verbosity = verbosity;
}

internal_function void
enable_channel(Logger *logger, Channel channel)
{
    U64 mask = U64(1) << (U64)channel;
    logger->channel_mask |= mask;
}

internal_function void
enable_all_channels(Logger *logger)
{
    logger->channel_mask = 0xFFFFFFFFFFFFFFFF;
}

internal_function void
disable_channel(Logger *logger, Channel channel)
{
    U64 mask = U64(1) << (U64)channel;
    logger->channel_mask &= ~mask;
}

internal_function void
disable_all_channels(Logger *logger)
{
    logger->channel_mask = 0;
}

internal_function void
debug_printf(Logger *logger, Channel channel,
             Verbosity verbosity, const char *format, ...)
{
    // note(amer): this is fine for now...
    local_presist char string_buffer[1024];

    va_list arg_list;
    va_start(arg_list, format);

    S32 written = vsnprintf(string_buffer, sizeof(string_buffer), format, arg_list);
    HE_Assert(written >= 0);

    Logging_Channel *main_channel = &logger->main_channel;

    if (platform_write_data_to_file(main_channel->log_file,
                                    main_channel->log_file_offset,
                                    (void*)string_buffer, written))
    {
        main_channel->log_file_offset += written;
    }

    Logging_Channel *logging_channel = &logger->channels[U8(channel)];
    if (platform_write_data_to_file(logging_channel->log_file,
                                    logging_channel->log_file_offset,
                                    (void*)string_buffer, written))
    {
        logging_channel->log_file_offset += written;
    }

    if ((logger->channel_mask & (U64(1) << channel)) && logger->verbosity >= verbosity)
    {
        platform_debug_printf(string_buffer);
    }

    va_end(arg_list);
}