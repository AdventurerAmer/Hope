#include "core/binary_stream.h"
#include "core/memory.h"
#include "core/file_system.h"

Binary_Stream binary_stream_from_arena(Memory_Arena *arena)
{
    return { .data = &arena->base[arena->offset], .offset = 0, .size = arena->size - arena->offset };
}

Binary_Stream binary_stream_from_file(Read_Entire_File_Result *file_result)
{
    HE_ASSERT(file_result->success);
    HE_ASSERT(file_result->size);
    return { .data = file_result->data, .offset = 0, .size = file_result->size };
}

void binary_stream_write(Binary_Stream *stream, const void *data, U64 size)
{
    HE_ASSERT(stream->offset + size <= stream->size);
    copy_memory((void *)&stream->data[stream->offset], data, size);
    stream->offset += size;
}

void binary_stream_read(Binary_Stream *stream, void *data, U64 size)
{
    HE_ASSERT(stream->offset + size <= stream->size);
    copy_memory(data, (const void*)&stream->data[stream->offset], size);
    stream->offset += size;
}

void binary_stream_write_string(Binary_Stream *stream, String str)
{
    U64 size = sizeof(U64) + sizeof(char) * str.count;
    HE_ASSERT(stream->offset + size <= stream->size);
    binary_stream_write(stream, &str.count, sizeof(U64));
    binary_stream_write(stream, str.data, sizeof(char) * str.count);
}

void binary_stream_read_string(Binary_Stream *stream, String *str)
{
    U64 size = sizeof(U64) + sizeof(char) * str->count;
    HE_ASSERT(stream->offset + size <= stream->size);
    binary_stream_read(stream, (void *)&str->count, sizeof(U64));
    str->data = (const char *)&stream->data[stream->offset];
    stream->offset += sizeof(char) * str->count;
}