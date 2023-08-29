#include "string.h"
#include "core/memory.h"

#include <stdio.h>
#include <stdarg.h>

U64 string_length(const char *str)
{
    U64 length = 0;
    while (str[length])
    {
        length++;
    }
    return length;
}

U64 hash(const String *str)
{
    const U64 p = 31;
    const U64 m = U64(1e9) + 7;

    U64 hash = 0;
    U64 multiplier = 1;
    for (U32 i = 0; i < str->count; i++)
    {
        hash = ((hash + (str->data[i] - 'a' + 1) * multiplier)) % m;
        multiplier = (multiplier * p) % m;
    }
    return hash;
}

// todo(amer): SIMD Version
bool equal(const char *a, U64 a_length, const char *b, U64 b_length)
{
    if (a_length != b_length)
    {
        return false;
    }
    for (U64 char_index = 0; char_index < a_length; char_index++)
    {
        if (a[char_index] != b[char_index])
        {
            return false;
        }
    }
    return true;
}

// todo(amer): SIMD version
S64 find_first_char_from_left(const String *str, const char *chars)
{
    U64 chars_count = string_length(chars);

    for (U64 i = 0; i < str->count; i++)
    {
        for (U64 j = 0; j < chars_count; j++)
        {
            if (str->data[i] == chars[j])
            {
                return i;
            }
        }
    }

    return -1;
}

// todo(amer): SIMD version
S64 find_first_char_from_right(const String *str, const char *chars)
{
    U64 chars_count = string_length(chars);

    for (U64 i = chars_count - 1; i >= 0; i--)
    {
        for (U64 j = 0; j < chars_count; j++)
        {
            if (str->data[i] == chars[j])
            {
                return i;
            }
        }
    }

    return -1;
}

String sub_string(const String *str, U64 index)
{
    HOPE_Assert(index < str->count);
    return { str->data + index, str->count - index };
}

String sub_string(const String *str, U64 index, U64 count)
{
    HOPE_Assert(index < str->count);
    HOPE_Assert(str->count - index >= count);
    return { str->data + index, count };
}

String format_string(struct Memory_Arena *arena, const char *format, va_list args)
{
    U8 *buffer = arena->base + arena->offset;
    S32 count = vsprintf((char *)buffer, format, args);
    HOPE_Assert(count >= 0);
    HOPE_Assert(arena->offset + count + 1 <= arena->size);
    arena->offset += count + 1;
    return { (const char *)buffer, (U64)count };
}

String format_string(Memory_Arena *arena, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    String result = format_string(arena, format, args);
    va_end(args);
    return result;
}


void begin_string_builder(String_Builder *string_builder, Memory_Arena *arena)
{
    HOPE_Assert(string_builder);
    HOPE_Assert(arena);

#ifndef HOPE_SHIPPING
    arena->current_temprary_owner = (Temprary_Memory_Arena *)string_builder;
#endif

    string_builder->arena = arena;
    string_builder->max_count = arena->size - arena->offset;
    string_builder->data = (char *)arena->base + arena->offset;
}

void push(String_Builder *string_builder, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    S32 count = vsprintf((char *)string_builder->data + string_builder->count, format, args);
    HOPE_Assert(count >= 0);
    HOPE_Assert(string_builder->count + count + 1 <= string_builder->max_count);
    string_builder->count += count;

    va_end(args);
}

String end_string_builder(String_Builder *string_builder)
{
    Memory_Arena *arena = string_builder->arena;
    arena->current_temprary_owner = nullptr;
    arena->offset += string_builder->count + 1;
    string_builder->data[string_builder->count] = 0;
    return { string_builder->data, string_builder->count };
}