#pragma once

#include "platform.h"
#include "logging.h"
#include "memory.h"

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
typedef void(*On_Event_Proc)(Engine *engine, Event input);
typedef void(*On_Update_Proc)(Engine *engine, F32 delta_time);

struct Game_Code
{
    Init_Game_Proc init_game;
    On_Event_Proc on_event;
    On_Update_Proc on_update;
};

typedef void*(*Allocate_Memory_Proc)(U64 size);
typedef void(*Debug_Printf)(const char *message, ...);

struct Platform
{
    Allocate_Memory_Proc allocate_memory;
    Debug_Printf debug_printf;
};

enum WindowMode : U8
{
    WindowMode_Windowed,
    WindowMode_Fullscreen
};

struct Engine
{
    Game_Memory memory;
    Game_Code game_code;
    Platform platform;
    bool show_cursor;
    WindowMode window_mode;
    bool is_running;
};

struct Engine_Configuration
{
    Mem_Size permanent_memory_size;
    Mem_Size transient_memory_size;
    WindowMode window_mode;
    bool show_cursor;
};

internal_function bool
startup(Engine *engine, const Engine_Configuration &configuration);

internal_function void
game_loop(Engine *engine, F32 delta_time);

internal_function void
shutdown(Engine *engine);