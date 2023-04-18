#include "engine.h"
#include "platform.h"

internal_function bool
startup(Engine *engine, const Engine_Configuration &configuration)
{
    // todo(amer): logging should only be enabled in non-shipping builds
    Logger *logger = &debug_state.main_logger;
    U64 channel_mask = 0xFFFFFFFFFFFFFFFF;
    bool logger_initied = init_logger(logger, "all.log",
                                      Verbosity_Trace, channel_mask);
    if (!logger_initied)
    {
        return false;
    }

    Mem_Size required_memory_size =
        configuration.permanent_memory_size + configuration.transient_memory_size;

    void *memory = platform_allocate_memory(required_memory_size);
    if (!memory)
    {
        return false;
    }

    U8 *permanent_memory = (U8 *)memory;
    engine->memory.permanent_memory_size = configuration.permanent_memory_size;
    engine->memory.permanent_arena = create_memory_arena(permanent_memory,
                                                         configuration.permanent_memory_size);

    U8 *transient_memory = (U8 *)memory + configuration.permanent_memory_size;
    engine->memory.transient_memory_size = configuration.transient_memory_size;
    engine->memory.transient_arena = create_memory_arena(transient_memory,
                                                         configuration.transient_memory_size);
    engine->show_cursor = configuration.show_cursor;
    engine->window_mode = configuration.window_mode;

    Platform_API *api = &engine->platform_api;
    api->allocate_memory = &platform_allocate_memory;
    api->deallocate_memory = &platform_deallocate_memory;
    api->open_file = &platform_open_file;
    api->is_file_handle_valid = &platform_is_file_handle_valid;
    api->read_data_from_file = &platform_read_data_from_file;
    api->write_data_to_file = &platform_write_data_to_file;
    api->close_file = &platform_close_file;
    api->debug_printf = &platform_debug_printf;

    Game_Code *game_code = &engine->game_code;
    bool game_initialized = game_code->init_game(engine);
    return game_initialized;
}

internal_function void
game_loop(Engine *engine, F32 delta_time)
{
    Game_Code *game_code = &engine->game_code;
    game_code->on_update(engine, delta_time);
}

internal_function void
shutdown(Engine *engine)
{
    (void)engine;
    // todo(amer): logging should only be enabled in non-shipping builds
    Logger *logger = &debug_state.main_logger;
    deinit_logger(logger);
}