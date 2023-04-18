#pragma once

#include "defines.h"

#if HE_OS_WINDOWS
#include "win32_platform.h"
#endif

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

    U16 width;
    U16 height;
};

internal_function void*
platform_allocate_memory(U64 size);

internal_function void
platform_deallocate_memory(void *memory);

internal_function Platform_File_Handle
platform_open_file(const char *filename, File_Operation operations);

internal_function bool
platform_is_file_handle_valid(Platform_File_Handle file_handle);

internal_function bool
platform_read_data_from_file(Platform_File_Handle file_handle, U64 offset, void *data, U64 size);

internal_function bool
platform_write_data_to_file(Platform_File_Handle file_handle, U64 offset, void *data, U64 size);

internal_function bool
platform_close_file(Platform_File_Handle file_handle);

internal_function void
platform_debug_printf(const char *message, ...);