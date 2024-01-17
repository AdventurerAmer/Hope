#pragma once

#include "platform.h"
#include "logging.h"
#include "memory.h"
#include "input.h"

#include "rendering/renderer.h"

struct Engine;

typedef bool(*Init_Game_Proc)(Engine *engine);
typedef void(*On_Event_Proc)(Engine *engine, Event event);
typedef void(*On_Update_Proc)(Engine *engine, F32 delta_time);

struct Game_Code
{
    Init_Game_Proc init_game;
    On_Event_Proc on_event;
    On_Update_Proc on_update;
};

struct Engine_API
{
    void (*init_camera)(Camera *camera, glm::vec3 position, glm::quat rotation, F32 aspect_ratio, F32 field_of_view, F32 near_clip, F32 far_clip);

    void (*init_fps_camera_controller)(FPS_Camera_Controller *camera_controller, F32 pitch, F32 yaw, F32 rotation_speed, F32 base_movement_speed, F32 max_movement_speed, F32 sensitivity_x, F32 sensitivity_y);
    void (*control_camera)(FPS_Camera_Controller *camera_controller, Camera *camera, const FPS_Camera_Controller_Input input, F32 delta_time);
    void (*update_camera)(Camera *camera);

    void* (*allocate_memory)(U64 size);
    void (*deallocate_memory)(void *memory);
    void (*set_window_mode)(Window *window, Window_Mode mode);
    void (*debug_printf)(const char *message);

    Render_Context (*get_render_context)();
};

struct Engine
{
    Game_Code game_code;

    String name;
    String app_name;

    bool is_running;
    bool is_minimized;
    bool show_cursor;
    bool lock_cursor;
    Window window;

    Input input;

    // todo(amer): move this to debug state
    bool show_imgui;

    /*
        note(amer): this is a platform specific pointer to Win32_Platform_State on windows
    */
    void *platform_state;

    Engine_API api;
};

bool startup(Engine *engine, void *platform_state);

void on_resize(Engine *engine, U32 window_width, U32 window_height, U32 client_width, U32 client_height);
void game_loop(Engine *engine, F32 delta_time);

void shutdown(Engine *engine);

// temprary
void finalize_asset_loads(Renderer *renderer, Renderer_State *renderer_state);