#include "file_system.h"

Read_Entire_File_Result read_entire_file(const char *filepath, Allocator allocator)
{
    Read_Entire_File_Result result = {};

    Open_File_Result open_file_result = platform_open_file(filepath, OpenFileFlag_Read);
    if (open_file_result.success)
    {
        U8 *data = nullptr;

        std::visit([&](auto &&allocator)
        {
            data = HE_ALLOCATE_ARRAY(allocator, U8, open_file_result.size);
        }, allocator);

        bool read = platform_read_data_from_file(&open_file_result, 0, data, open_file_result.size);
        HE_ASSERT(read);
        platform_close_file(&open_file_result);

        result.data = data;
        result.size = open_file_result.size;
        result.success = true;
    }
    return result;
}


bool write_entire_file(const char *filepath, void *data, Size size)
{
    Open_File_Result open_file_result = platform_open_file(filepath, Open_File_Flags(OpenFileFlag_Write|OpenFileFlag_Truncate));
    if (!open_file_result.success)
    {
        return false;
    }
    bool success = platform_write_data_to_file(&open_file_result, 0, data, size);
    HE_ASSERT(success);
    platform_close_file(&open_file_result);
    return success;
}