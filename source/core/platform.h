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


enum File_Operation : U32
{
    FileOperation_Read  = 0x1,
    FileOperation_Write = 0x2
};

struct Platform_File_Handle
{
    void *platform_data;
};

void* platform_allocate_memory(U64 size);

void platform_deallocate_memory(void *memory);

Platform_File_Handle platform_open_file(const char *filename, File_Operation operations);

bool platform_is_file_handle_valid(Platform_File_Handle file_handle);

U64 platform_get_file_size(Platform_File_Handle file_handle);

bool platform_read_data_from_file(Platform_File_Handle file_handle, U64 offset, void *data, U64 size);

bool platform_write_data_to_file(Platform_File_Handle file_handle, U64 offset, void *data, U64 size);

bool platform_close_file(Platform_File_Handle file_handle);

struct Read_Entire_File_Result
{
    Platform_File_Handle file_handle;
    U64 size;
    bool success;
};

Read_Entire_File_Result platform_begin_read_entire_file(const char *filename);

bool platform_end_read_entire_file(Read_Entire_File_Result *read_entire_file_result, void *data);

void platform_toggle_fullscreen(struct Engine *engine);

void* platform_create_vulkan_surface(struct Engine *engine, void *instance);

void platform_report_error_and_exit(const char *message, ...);

void platform_debug_printf(const char *message, ...);