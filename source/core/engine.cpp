#include "engine.h"
#include "platform.h"
#include "rendering/vulkan.h"

internal_function bool
startup(Engine *engine, const Engine_Configuration &configuration,
        void *platform_state)
{
    // todo(amer): logging should only be enabled in non-shipping builds
    Logger *logger = &debug_state.main_logger;
    U64 channel_mask = 0xFFFFFFFFFFFFFFFF;
    bool logger_initied = init_logger(logger, "all",
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
    engine->platform_state = platform_state;

    if (engine->window_mode == WindowMode_Fullscreen)
    {
        platform_toggle_fullscreen(engine);
    }

    engine->vulkan_context = ArenaPush(&engine->memory.permanent_arena, Vulkan_Context);
    bool vulkan_inited = init_vulkan(engine->vulkan_context, engine, &engine->memory.permanent_arena);
    if (!vulkan_inited)
    {
        return false;
    }

    Platform_API *api = &engine->platform_api;
    api->allocate_memory = &platform_allocate_memory;
    api->deallocate_memory = &platform_deallocate_memory;
    api->open_file = &platform_open_file;
    api->is_file_handle_valid = &platform_is_file_handle_valid;
    api->read_data_from_file = &platform_read_data_from_file;
    api->write_data_to_file = &platform_write_data_to_file;
    api->close_file = &platform_close_file;
    api->debug_printf = &platform_debug_printf;
    api->toggle_fullscreen = &platform_toggle_fullscreen;

    Game_Code *game_code = &engine->game_code;
    bool game_initialized = game_code->init_game(engine);
    return game_initialized;
}

internal_function void
game_loop(Engine *engine, F32 delta_time)
{
    Game_Code *game_code = &engine->game_code;
    game_code->on_update(engine, delta_time);

    if (!engine->is_minimized)
    {
        vulkan_draw(engine->vulkan_context, engine->back_buffer_width, engine->back_buffer_height);
    }
}

internal_function void
shutdown(Engine *engine)
{
    deinit_vulkan(engine->vulkan_context);

    (void)engine;
    // todo(amer): logging should only be enabled in non-shipping builds
    Logger *logger = &debug_state.main_logger;
    deinit_logger(logger);
}

internal_function void
set_game_code_to_stubs(Game_Code *game_code)
{
    game_code->init_game = &init_game_stub;
    game_code->on_event = &on_event_stub;
    game_code->on_update = &on_update_stub;
}

internal_function bool
init_game_stub(Engine *engine)
{
    (void)engine;
    return true;
}

internal_function void
on_event_stub(Engine *engine, Event event)
{
    (void)engine;
    (void)event;
}

internal_function void
on_update_stub(Engine *engine, F32 delta_time)
{
    (void)engine;
    (void)delta_time;
}