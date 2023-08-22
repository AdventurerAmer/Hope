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

// todo(amer): SIMD Version
bool equal(const String *a, const String *b)
{
    if (a->count != b->count)
    {
        return false;
    }

    for (U32 char_index = 0; char_index < a->count; char_index++)
    {
        if (a->data[char_index] != b->data[char_index])
        {
            return false;
        }
    }

    return true;
}

// todo(amer): SIMD Version
S64 find_first_char_from_left(const String *str, const char *chars)
{
    U64 chars_count = string_length(chars);

    for (U64 i = 0; i < chars_count; i++)
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