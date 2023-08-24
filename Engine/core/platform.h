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


enum Open_File_Flags : U32
{
    OpenFileFlag_Read = 0x1,
    OpenFileFlag_Write = 0x2,
    OpenFileFlag_Truncate = 0x4,
};

void* platform_allocate_memory(Size size);

void platform_deallocate_memory(void *memory);

bool platform_file_exists(const char *filepath);

struct Open_File_Result
{
    void *file_handle;
    Size size;
    bool success;
};

Open_File_Result platform_open_file(const char *filepath, Open_File_Flags open_file_flags);

bool platform_read_data_from_file(const Open_File_Result *open_file_result, Size offset, void *data, Size size);

bool platform_write_data_to_file(const Open_File_Result *open_file_result, Size offset, void *data, Size size);

bool platform_close_file(Open_File_Result *open_file_result);

void platform_toggle_fullscreen(struct Engine *engine);

void* platform_create_vulkan_surface(struct Engine *engine,
                                     void *instance, const
                                     void *allocator_callbacks = nullptr);

void platform_init_imgui(struct Engine *engine);
void platform_imgui_new_frame();
void platform_shutdown_imgui();

void platform_debug_printf(const char *message);