#pragma once

#include "core/defines.h"
#include "containers/string.h"

void binary_stream_write(struct Binary_Stream *stream, const void *data, U64 size);
void binary_stream_read(struct Binary_Stream *stream, void *data, U64 size);

void binary_stream_write_string(struct Binary_Stream *stream, String str);
void binary_stream_read_string(struct Binary_Stream *stream, String *str);

struct Binary_Stream
{
    U8 *data;
    U64 offset;
    U64 size;

    template< typename T >
    void write(const T *data)
    {
        binary_stream_write(this, (const void *)data, sizeof(T));
    }

    template<>
    void write<String>(const String *str)
    {
        binary_stream_write_string(this, *str);
    }

    template< typename T >
    void read(T *data)
    {
        binary_stream_read(this, (void *)data, sizeof(T));
    }

    template< >
    void read< String >(String *data)
    {
        binary_stream_read_string(this, data);
    }
};

Binary_Stream binary_stream_from_arena(struct Memory_Arena *arena);
Binary_Stream binary_stream_from_file(struct Read_Entire_File_Result *file_result);