#include "file_system.h"

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