#pragma once

#include "defines.h"

enum Event_Type : U8
{
    EventType_Unknown,
    EventType_Key,
    EventType_Mouse,
    EventType_Resize,
    EventType_Close
};

struct Event
{
    Event_Type type;

    union
    {
        U16 key;
        U16 button;
    };

    bool pressed;
    bool held;

    bool is_shift_down;
    bool is_control_down;
    bool double_click;
    U16 mouse_x;
    U16 mouse_y;

    bool mouse_wheel_up;
    bool mouse_wheel_down;

    bool minimized;
    bool maximized;
    bool restored;

    U16 width;
    U16 height;
};



void* platform_allocate_memory(Size size);

void platform_deallocate_memory(void *memory);

enum class Window_Mode : U8
{
    WINDOWED   = 0,
    FULLSCREEN = 1
};

struct Window
{
    U32 width;
    U32 height;
    const char *title;
    Window_Mode mode;

    void *platform_window_state;
};

bool platform_create_window(Window *window, const char *title, U32 width, U32 height, Window_Mode window_mode = Window_Mode::WINDOWED);
void platform_set_window_mode(Window *window, Window_Mode window_mode);

enum Open_File_Flags : U8
{
    OpenFileFlag_Read     = 1 << 0,
    OpenFileFlag_Write    = 1 << 1,
    OpenFileFlag_Truncate = 1 << 2,
};

bool platform_file_exists(const char *filepath);

struct Open_File_Result
{
    void *handle;
    Size size;
    bool success;
};

Open_File_Result platform_open_file(const char *filepath, Open_File_Flags open_file_flags);

bool platform_read_data_from_file(const Open_File_Result *open_file_result, Size offset, void *data, Size size);

bool platform_write_data_to_file(const Open_File_Result *open_file_result, Size offset, void *data, Size size);

bool platform_close_file(Open_File_Result *open_file_result);

struct Dynamic_Library
{
    void *platform_dynamic_library_state;
};

bool platform_load_dynamic_library(Dynamic_Library *dynamic_library, const char *filepath);
void *platform_get_proc_address(Dynamic_Library *dynamic_library, const char *proc_name);
bool platform_unload_dynamic_library(Dynamic_Library *dynamic_library);

void* platform_create_vulkan_surface(struct Engine *engine,
                                     void *instance,
                                     const void *allocator_callbacks = nullptr);

void platform_init_imgui(struct Engine *engine);
void platform_imgui_new_frame();
void platform_shutdown_imgui();

void platform_debug_printf(const char *message);