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

typedef void* (*Allocate_Memory_Proc)(U64 size);

typedef void (*Deallocate_Memory_Proc)(void *memory);

typedef void (*Toggle_Fullscreen_Proc)(struct Engine *engine);

typedef void (*Debug_Printf)(const char *message);

struct Platform_API
{
    Allocate_Memory_Proc allocate_memory;
    Deallocate_Memory_Proc deallocate_memory;
    Debug_Printf debug_printf;
};

typedef void* (*imgui_mem_alloc_proc)(size_t sz, void* user_data);
typedef void  (*imgui_mem_free_proc)(void* ptr, void* user_data);

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

    Scene_Node* (*load_model)(const char *path, Renderer *renderer,
                              Renderer_State *renderer_state);

    void (*render_scene_node)(Renderer *renderer, Renderer_State *renderer_state,
                              Scene_Node *scene_node, glm::mat4 transform);

    bool (*imgui_begin_window)(const char* name, bool *p_open, int flags);
    void (*imgui_end_window)();
};

struct Engine
{
    Platform_API platform_api;
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

// todo(amer): move to file_utils.h
struct Read_Entire_File_Result
{
    bool success;
    U8 *data;
    Size size;
};

template< typename Allocator >
Read_Entire_File_Result read_entire_file(const char *filepath, Allocator *allocator)
{
    Read_Entire_File_Result result = {};

    Open_File_Result open_file_result = platform_open_file(filepath, OpenFileFlag_Read);
    if (open_file_result.success)
    {
        U8 *data = AllocateArray(allocator, U8, open_file_result.size);
        bool read = platform_read_data_from_file(&open_file_result, 0, data, open_file_result.size);
        HOPE_Assert(read);
        platform_close_file(&open_file_result);

        result.data = data;
        result.size = open_file_result.size;
        result.success = true;
    }
    return result;
}

bool write_entire_file(const char *filepath, void *data, Size size);
