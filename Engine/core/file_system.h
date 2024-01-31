#pragma once

#include "defines.h"
#include "memory.h"
#include "containers/string.h"
#include "containers/dynamic_array.h"
#include "platform.h"

void sanitize_path(String &path);
bool file_exists(const String &path);
bool directory_exists(const String &path);

String get_current_working_directory(auto allocator)
{
    U64 size = 0;
    platform_get_current_working_directory(nullptr, &size);
    char *data = HE_ALLOCATE_ARRAY(allocator, char, size);
    platform_get_current_working_directory(data, &size);
    return { data, size - 1 };
}

String get_parent_path(const String &path);
String get_extension(const String &path);
String get_name(const String &path);

struct Read_Entire_File_Result
{
    bool success;
    U8 *data;
    U64 size;
};

Read_Entire_File_Result read_entire_file(const char *filepath, auto allocator)
{
    Read_Entire_File_Result result = {};

    Open_File_Result open_file_result = platform_open_file(filepath, OpenFileFlag_Read);
    if (open_file_result.success)
    {
        U8 *data = HE_ALLOCATE_ARRAY(allocator, U8, open_file_result.size);

        bool read = platform_read_data_from_file(&open_file_result, 0, data, open_file_result.size);
        HE_ASSERT(read);
        platform_close_file(&open_file_result);

        result.data = data;
        result.size = open_file_result.size;
        result.success = true;
    }

    return result;
}

bool write_entire_file(const char *filepath, void *data, U64 size);