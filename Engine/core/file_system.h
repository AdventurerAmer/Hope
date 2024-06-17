#pragma once

#include "defines.h"
#include "memory.h"
#include "containers/string.h"
#include "containers/array_view.h"
#include "platform.h"

void sanitize_path(String &path);
bool file_exists(String path);
bool directory_exists(String path);
String open_file_dialog(String title, String filter, Array_View< String > extensions);
String save_file_dialog(String title, String filter, Array_View< String > extensions);

String get_current_working_directory(Allocator allocator);
String get_parent_path(String path);
String get_extension(String path);
String get_name(String path);
String get_name_with_extension(String path);

struct Read_Entire_File_Result
{
    bool success;
    U8 *data;
    U64 size;
};

Read_Entire_File_Result read_entire_file(String path, Allocator allocator);
bool write_entire_file(String path, void *data, U64 size);