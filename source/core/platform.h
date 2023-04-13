#pragma once

#include "core/defines.h"

#if HE_OS_WINDOWS
#include "win32_platform.h"
#endif

internal_function void*
platform_allocate_memory(U64 size);

internal_function void
platform_deallocate_memory(void *memory);

enum File_Operation
{
    File_Operation_Read = (1 << 0),
    File_Operation_Write = (1 << 1)
};

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
platform_debug_printf(const char *message);