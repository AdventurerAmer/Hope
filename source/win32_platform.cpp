#include "win32_platform.h"
#include "core/platform.h"
#include "core/memory.h"

internal_function void*
platform_allocate_memory(U64 size)
{
    HE_Assert(size);
    return VirtualAlloc(0, size, MEM_COMMIT, PAGE_READWRITE);
}

internal_function void
platform_deallocate_memory(void *memory)
{
    HE_Assert(memory);
    VirtualFree(memory, 0, MEM_RELEASE);
}

internal_function Platform_File_Handle
platform_open_file(const char *filename, File_Operation operations)
{
    Platform_File_Handle result = {};

    DWORD access_flags = operations;
    result.win32_file_handle = CreateFileA(filename, access_flags, FILE_SHARE_READ,
                                           0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    return result;
}

internal_function bool
platform_is_file_handle_valid(Platform_File_Handle file_handle)
{
    bool result = file_handle.win32_file_handle != INVALID_HANDLE_VALUE;
    return result;
}

internal_function U64
platform_get_file_size(Platform_File_Handle file_handle)
{
    LARGE_INTEGER result = {};
    BOOL return_value = GetFileSizeEx(file_handle.win32_file_handle, &result);
    HE_Assert(return_value);
    return result.QuadPart;
}

internal_function bool
platform_read_data_from_file(Platform_File_Handle file_handle, U64 offset, void *data, U64 size)
{
    OVERLAPPED overlapped = {};
    overlapped.Offset = u64_to_u32(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = u64_to_u32(offset >> 32);

    // note(amer): we are only limited to a read of 4GBs
    DWORD read_bytes;
    BOOL result = ReadFile(file_handle.win32_file_handle, data,
                           u64_to_u32(size), &read_bytes, &overlapped);
    return result == TRUE && read_bytes == size;
}

internal_function bool
platform_write_data_to_file(Platform_File_Handle file_handle, U64 offset, void *data, U64 size)
{
    OVERLAPPED overlapped = {};
    overlapped.Offset = u64_to_u32(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = u64_to_u32(offset >> 32);

    // note(amer): we are only limited to a write of 4GBs
    DWORD written_bytes;
    BOOL result = WriteFile(file_handle.win32_file_handle, data,
                            u64_to_u32(size), &written_bytes, &overlapped);
    return result == TRUE && written_bytes == size;
}

internal_function bool
platform_close_file(Platform_File_Handle file_handle)
{
    bool result = CloseHandle(file_handle.win32_file_handle) != 0;
    return result;
}

internal_function Read_Entire_File_Result
platform_begin_read_entire_file(const char *filename)
{
    Read_Entire_File_Result result = {};
    HANDLE win32_file_handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ,
                                           0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (win32_file_handle != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER size_result = {};
        if (GetFileSizeEx(win32_file_handle, &size_result))
        {
            U64 size = size_result.QuadPart;
            if (size)
            {
                result.file_handle.win32_file_handle = win32_file_handle;
                result.size = size;
                result.success = true;
            }
            else
            {
                CloseHandle(win32_file_handle);
            }
        }
    }

    return result;
}

internal_function bool
platform_end_read_entire_file(Read_Entire_File_Result *read_entire_file_result,
                              void *data)
{
    // note(amer): we are only limited to a write of 4GBs
    DWORD read_bytes;
    BOOL result = ReadFile(read_entire_file_result->file_handle.win32_file_handle, data,
                           u64_to_u32(read_entire_file_result->size), &read_bytes, NULL);
    CloseHandle(read_entire_file_result->file_handle.win32_file_handle);
    return result == TRUE && read_bytes == read_entire_file_result->size;
}

internal_function void
platform_report_error_and_exit(const char *message, ...)
{
    local_presist char string_buffer[1024];
    va_list arg_list;

    va_start(arg_list, message);
    S32 written = vsprintf(string_buffer, message, arg_list);
    HE_Assert(written >= 0);

    MessageBoxA(NULL, string_buffer, TEXT("Error"), MB_OK);

    va_end(arg_list);
}

internal_function void
platform_debug_printf(const char *message, ...)
{
    local_presist char string_buffer[1024];
    va_list arg_list;

    va_start(arg_list, message);
    S32 written = vsprintf(string_buffer, message, arg_list);
    HE_Assert(written >= 0);
    OutputDebugStringA(string_buffer);
    va_end(arg_list);
}