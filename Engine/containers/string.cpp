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

U64 hash_key(String str)
{
    const U64 p = 31;
    const U64 m = U64(1e9) + 7;

    U64 hash = 0;
    U64 multiplier = 1;
    for (U32 i = 0; i < str.count; i++)
    {
        hash = ((hash + (str.data[i] - 'a' + 1) * multiplier)) % m;
        multiplier = (multiplier * p) % m;
    }
    return hash;
}

String copy_string(const char *str, U64 count, Allocator allocator)
{
    HE_ASSERT(str);
    HE_ASSERT(count);
    char *data = HE_ALLOCATOR_ALLOCATE_ARRAY(&allocator, char, count + 1);
    copy_memory(data, str, count);
    data[count] = 0;
    return { data, count };
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
S64 find_first_char_from_left(String str, String chars, U64 offset)
{
    HE_ASSERT(offset <= str.count);

    for (U64 i = offset; i < str.count; i++)
    {
        for (U64 j = 0; j < chars.count; j++)
        {
            if (str.data[i] == chars.data[j])
            {
                return i;
            }
        }
    }

    return -1;
}

// todo(amer): SIMD version
S64 find_first_char_from_right(String str, String chars)
{
    for (S64 i = str.count - 1; i >= 0; i--)
    {
        for (U64 j = 0; j < chars.count; j++)
        {
            if (str.data[i] == chars.data[j])
            {
                return i;
            }
        }
    }

    return -1;
}

bool starts_with(String str, String start)
{
    if (start.count > str.count)
    {
        return false;
    }
    return sub_string(str, 0, start.count) == start;
}

bool ends_with(String str, String end)
{
    if (end.count > str.count)
    {
        return false;
    }

    U64 offset = str.count - end.count;
    return sub_string(str, offset, end.count) == end;
}

String sub_string(String str, U64 index)
{
    HE_ASSERT(index < str.count);
    return { str.data + index, str.count - index };
}

String sub_string(String str, U64 index, U64 count)
{
    HE_ASSERT(index < str.count);
    HE_ASSERT(str.count - index >= count);
    return { str.data + index, count };
}

String format_string(Memory_Arena *arena, const char *format, va_list args)
{
    U8 *buffer = arena->base + arena->offset;
    S32 count = vsprintf((char *)buffer, format, args);
    HE_ASSERT(count >= 0);
    HE_ASSERT(arena->offset + count + 1 <= arena->size);
    arena->offset += count + 1;
    return { (const char *)buffer, (U64)count };
}

String advance(String str, U64 count)
{
    HE_ASSERT(count <= str.count);
    return { .data = str.data + count, .count = str.count - count }; 
}

String eat_chars(String str, String chars)
{
    for (U64 i = 0; i < str.count; i++)
    {
        bool should_eat = false;

        for (U64 j = 0; j < chars.count; j++)
        {
            if (str.data[i] == chars.data[j])
            {
                should_eat = true;
                break;
            }
        }

        if (!should_eat)
        {
            return sub_string(str, i);
        }
    }

    return { };
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
    HE_ASSERT(string_builder);
    HE_ASSERT(arena);

    string_builder->arena = arena;
    string_builder->max_count = arena->size - arena->offset;
    string_builder->data = (char *)arena->base + arena->offset;
}

void append(String_Builder *string_builder, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    S32 count = vsprintf((char *)string_builder->data + string_builder->count, format, args);
    HE_ASSERT(count >= 0);
    HE_ASSERT(string_builder->count + count + 1 <= string_builder->max_count);
    string_builder->count += count;

    va_end(args);
}

String end_string_builder(String_Builder *string_builder)
{
    Memory_Arena *arena = string_builder->arena;
    arena->offset += string_builder->count + 1;
    string_builder->data[string_builder->count] = 0;
    return { string_builder->data, string_builder->count };
}