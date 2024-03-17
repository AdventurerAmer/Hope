#pragma once

#include "defines.h"
#include "memory.h"
#include "containers/string.h"
#include "containers/array_view.h"
#include "platform.h"

void sanitize_path(String &path);
bool file_exists(const String &path);
bool directory_exists(const String &path);
String open_file_dialog(String title, String filter, Array_View<String> patterns, Memory_Arena *arena);

String get_current_working_directory(Allocator allocator);
String get_parent_path(const String &path);
String get_extension(const String &path);
String get_name(const String &path);

struct Read_Entire_File_Result
{
    bool success;
    U8 *data;
    U64 size;
};

Read_Entire_File_Result read_entire_file(const char *filepath, Allocator *allocator);
bool write_entire_file(const char *filepath, void *data, U64 size);