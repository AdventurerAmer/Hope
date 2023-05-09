#pragma once

#include "platform.h"
#include "logging.h"
#include "memory.h"
#include "rendering/renderer.h"

struct Game_Memory
{
    Mem_Size permanent_memory_size;
    void *permenent_memory;

    Mem_Size transient_memory_size;
    void *transient_memory;

    Memory_Arena permanent_arena;
    Memory_Arena transient_arena;
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

internal_function void
set_game_code_to_stubs(Game_Code *game_code);

internal_function bool
init_game_stub(Engine *engine);

internal_function void
on_event_stub(Engine *engine, Event event);

internal_function void
on_update_stub(Engine *engine, F32 delta_time);

typedef void* (*Allocate_Memory_Proc)(U64 size);

typedef void (*Deallocate_Memory_Proc)(void *memory);

typedef Platform_File_Handle (*Open_File_Proc)(const char *filename,
                                               File_Operation operations);

typedef bool (*Is_File_Handle_Valid_Proc)(Platform_File_Handle file_handle);

typedef bool (*Read_Data_From_File_Proc)(Platform_File_Handle file_handle,
                                         U64 offset, void *data, U64 size);

typedef bool (*Write_Data_To_File_Proc)(Platform_File_Handle file_handle,
                                        U64 offset, void *data, U64 size);

typedef bool (*Close_File_Proc)(Platform_File_Handle file_handle);

typedef void (*Toggle_Fullscreen_Proc)(struct Engine *engine);

typedef void (*Debug_Printf)(const char *message, ...);

struct Platform_API
{
    Allocate_Memory_Proc allocate_memory;
    Deallocate_Memory_Proc deallocate_memory;
    Open_File_Proc open_file;
    Is_File_Handle_Valid_Proc is_file_handle_valid;
    Read_Data_From_File_Proc read_data_from_file;
    Write_Data_To_File_Proc write_data_to_file;
    Close_File_Proc close_file;
    Toggle_Fullscreen_Proc toggle_fullscreen;
    Debug_Printf debug_printf;
};

enum WindowMode : U8
{
    WindowMode_Windowed,
    WindowMode_Fullscreen
};

struct Engine_Configuration
{
    Mem_Size permanent_memory_size;
    Mem_Size transient_memory_size;
    WindowMode window_mode;
    bool show_cursor;
    U32 back_buffer_width;
    U32 back_buffer_height;
};

struct Engine
{
    Platform_API platform_api;
    Game_Memory memory;
    Game_Code game_code;

    bool is_running;
    bool is_minimized;
    bool show_cursor;
    WindowMode window_mode;

    Renderer_State renderer_state;
    Renderer renderer;
    /*
        note(amer): this is a platform specific pointer to Win32_State on windows
    */
    void *platform_state;
};

internal_function bool
startup(Engine *engine, const Engine_Configuration &configuration,
        void *platform_state);

internal_function void
game_loop(Engine *engine, F32 delta_time);

internal_function void
shutdown(Engine *engine);