#pragma once

#include "defines.h"
#include "platform.h"

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