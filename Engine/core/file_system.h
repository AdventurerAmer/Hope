#pragma once

#include "defines.h"
#include "memory.h"
#include "platform.h"

struct Read_Entire_File_Result
{
    bool success;
    U8 *data;
    Size size;
};

Read_Entire_File_Result read_entire_file(const char *filepath, Allocator allocator);
bool write_entire_file(const char *filepath, void *data, Size size);