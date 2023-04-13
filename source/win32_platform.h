#pragma once

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN 1
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#pragma warning(push, 0)
#include <strsafe.h>
#include <windows.h>
#pragma warning(pop)

struct Platform_File_Handle
{
    HANDLE win32_file_handle;
};