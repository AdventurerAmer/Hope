#include "engine.h"
#include "platform.h"
#include "rendering/renderer.h"
#include "rendering/renderer_utils.h"
#include "logging.h"
#include "cvars.h"
#include "job_system.h"
#include "file_system.h"

// #include "resources/resource_system.h"
#include "assets/asset_manager.h"

#include <chrono>
#include <imgui.h>

#include <stb/stb_image.h>

bool hope_app_init(Engine *engine);
void hope_app_on_event(Engine *engine, Event event);
void hope_app_on_update(Engine *engine, F32 delta_time);
void hope_app_shutdown(Engine *engine);

bool startup(Engine *engine)
{
    bool inited = init_memory_system();
    if (!inited)
    {
        return false;
    }

    init_logging_system();
    
    init_cvars(HE_STRING_LITERAL("config.cvars"));
    
    engine->show_cursor = false;
    engine->lock_cursor = false;

    engine->name = HE_STRING_LITERAL("Hope");
    engine->app_name = HE_STRING_LITERAL("Hope");

    String &engine_name = engine->name;
    String &app_name = engine->app_name;
    
    Window *window = &engine->window;
    window->width = 1296;
    window->height = 759;
    window->mode = Window_Mode::WINDOWED;
    window->maximized = false;

    U32 &window_width = window->width;
    U32 &window_height = window->height;
    U8 &window_mode = (U8 &)window->mode;
    bool &maximized = window->maximized;
    
    HE_DECLARE_CVAR("platform", engine_name, CVarFlag_None);
    HE_DECLARE_CVAR("platform", app_name, CVarFlag_None);
    HE_DECLARE_CVAR("platform", window_width, CVarFlag_None);
    HE_DECLARE_CVAR("platform", window_height, CVarFlag_None);
    HE_DECLARE_CVAR("platform", window_mode, CVarFlag_None);
    HE_DECLARE_CVAR("platform", maximized, CVarFlag_None);

    bool window_created = platform_create_window(window, app_name.data, (U32)window_width, (U32)window_height, maximized, (Window_Mode)window_mode);
    if (!window_created)
    {
        HE_LOG(Core, Fetal, "failed to create window\n");
        return false;
    }

    bool input_inited = init_input(&engine->input);
    if (!input_inited)
    {
        HE_LOG(Core, Fetal, "failed to initialize input system\n");
        return false;
    }

    bool job_system_inited = init_job_system();
    if (!job_system_inited)
    {
        HE_LOG(Core, Fetal, "failed to initialize job system\n");
        return false;
    }

    bool renderer_state_inited = init_renderer_state(engine);
    if (!renderer_state_inited)
    {
        HE_LOG(Core, Fetal, "failed to initialize render system\n");
        return false;
    }

    bool asset_manager_inited = init_asset_manager(HE_STRING_LITERAL("assets"));

    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;
    Renderer *renderer = render_context.renderer;
    
    bool app_inited = hope_app_init(engine);
    return app_inited;
}

void on_event(Engine *engine, Event event)
{
    switch (event.type)
    {
        case Event_Type::RESIZE:
        {
            Window *window = &engine->window;
            window->width = event.window_width;
            window->height = event.window_height;
            window->maximized = event.maximized;
            renderer_on_resize(event.client_width, event.client_height);
        } break;
    }

    hope_app_on_event(engine, event);
}

void game_loop(Engine *engine, F32 delta_time)
{
    Memory_Arena *frame_arena = get_frame_arena();
    Temprary_Memory frame_temprary_memory = begin_temprary_memory(frame_arena);

    renderer_handle_upload_requests();

    if (!engine->is_minimized)
    {
        imgui_new_frame();
    }

    hope_app_on_update(engine, delta_time);

    end_temprary_memory(&frame_temprary_memory);
}

void shutdown(Engine *engine)
{
    hope_app_shutdown(engine);

    deinit_asset_manager();

    deinit_renderer_state();

    deinit_job_system();

    deinit_cvars();

    deinit_logging_system();

    deinit_memory_system();
}