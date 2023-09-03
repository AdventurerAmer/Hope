#pragma once

#include "platform.h"
#include "logging.h"
#include "memory.h"
#include "input.h"

#include "rendering/renderer.h"

struct Game_Memory
{
    Size permanent_memory_size;
    void *permenent_memory;

    Size transient_memory_size;
    void *transient_memory;

    Memory_Arena permanent_arena;
    Memory_Arena transient_arena;
    Free_List_Allocator free_list_allocator;
};

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
    void (*init_camera)(Camera *camera, glm::vec3 position,
                        glm::quat rotation, F32 aspect_ratio,
                        F32 field_of_view, F32 near_clip, F32 far_clip);

    void (*init_fps_camera_controller)(FPS_Camera_Controller *camera_controller,
                                       F32 pitch, F32 yaw, F32 rotation_speed,
                                       F32 base_movement_speed,
                                       F32 max_movement_speed,
                                       F32 sensitivity_x,
                                       F32 sensitivity_y);

    void (*control_camera)(FPS_Camera_Controller *camera_controller,
                           Camera *camera,
                           const FPS_Camera_Controller_Input input,
                           F32 delta_time);

    void (*update_camera)(Camera *camera);

    Scene_Node* (*load_model)(const String &path, Renderer *renderer,
                              Renderer_State *renderer_state, Memory_Arena *arena);

    Scene_Node* (*load_model_threaded)(const String &path, Renderer *renderer, Renderer_State *renderer_state);

    void (*render_scene_node)(Renderer *renderer, Renderer_State *renderer_state,
                              Scene_Node *scene_node, const glm::mat4 &transform);

    void* (*allocate_memory)(U64 size);
    void  (*deallocate_memory)(void *memory);
    void  (*set_window_mode)(Window *window, Window_Mode mode);
    void  (*debug_printf)(const char *message);
};

struct Engine
{
    Game_Memory memory;
    Game_Code game_code;

    bool is_running;
    bool is_minimized;
    bool show_cursor;
    bool lock_cursor;
    Window window;

    Input input;

    Renderer_State renderer_state;
    Renderer renderer;

    bool show_imgui;
    bool imgui_docking;

    /*
        note(amer): this is a platform specific pointer to Win32_Platform_State on windows
    */
    void *platform_state;

    Engine_API api;
};

bool startup(Engine *engine, void *platform_state);

void game_loop(Engine *engine, F32 delta_time);

void shutdown(Engine *engine);
