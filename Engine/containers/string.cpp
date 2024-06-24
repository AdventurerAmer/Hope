#include "string.h"
#include "core/memory.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static constexpr String white_space = HE_STRING_LITERAL(" \n\t\r\v\f");

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
    char *data = HE_ALLOCATOR_ALLOCATE_ARRAY(allocator, char, count + 1);
    copy_memory(data, str, count);
    data[count] = 0;
    return { .count = count, .data = data };
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
    return { str.count - index, str.data + index };
}

String sub_string(String str, U64 index, U64 count)
{
    HE_ASSERT(index < str.count);
    HE_ASSERT(str.count - index >= count);
    return { count, str.data + index };
}

String format_string(Memory_Arena *arena, const char *format, va_list args)
{
    U8 *buffer = arena->base + arena->offset;
    S32 count = vsprintf((char *)buffer, format, args);
    HE_ASSERT(count >= 0);
    HE_ASSERT(arena->offset + count + 1 <= arena->size);
    arena->offset += count + 1;
    return { (U64)count, (const char *)buffer };
}

String advance(String str, U64 count)
{
    HE_ASSERT(count <= str.count);
    return { .count = str.count - count, .data = str.data + count };
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

    return {};
}

String eat_none_of_chars(String str, String chars)
{
    for (U64 i = 0; i < str.count; i++)
    {
        bool should_eat = true;

        for (U64 j = 0; j < chars.count; j++)
        {
            if (str.data[i] == chars.data[j])
            {
                should_eat = false;
                break;
            }
        }

        if (!should_eat)
        {
            return sub_string(str, i);
        }
    }

    return str;
}

String eat_white_space(String str)
{
    return eat_chars(str, white_space);
}

String eat_none_white_space(String *str)
{
    String result = *str;
    *str = eat_none_of_chars(*str, white_space);
    result.count -= str->count;
    return result;
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
    return { string_builder->count, string_builder->data };
}

Parse_Name_Value_Result parse_name_value(String *str, String name)
{
    *str = eat_chars(*str, white_space);
    if (!starts_with(*str, name))
    {
        return {};
    }

    *str = advance(*str, name.count);
    *str = eat_chars(*str, white_space);

    S64 index = find_first_char_from_left(*str, white_space);
    if (index == -1)
    {
        return {};
    }

    String value = sub_string(*str, 0, index);
    *str = advance(*str, value.count);
    *str = eat_chars(*str, white_space);

    return { .success = true, .value = value };
}

Parse_Name_Float3_Result parse_name_float3(String *str, String name)
{
    *str = eat_white_space(*str);
    if (!starts_with(*str, name))
    {
        return {};
    }

    *str = advance(*str, name.count);
    *str = eat_white_space(*str);

    Parse_Name_Float3_Result result = { .success = true };

    for (U32 i = 0; i < 3; i++)
    {
        String value = eat_none_white_space(str);
        result.values[i] = str_to_f32(value);
        *str = eat_white_space(*str);
    }

    *str = eat_white_space(*str);
    return result;
}

U64 str_to_u64(String str)
{
    Memory_Context memory_context = get_memory_context();
    String temp = copy_string(str, memory_context.temp);
    return strtoull(temp.data, nullptr, 10);
}

S64 str_to_s64(String str)
{
    Memory_Context memory_context = get_memory_context();
    String temp = copy_string(str, memory_context.temp);
    return strtoull(temp.data, nullptr, 10);;
}

F32 str_to_f32(String str)
{
    Memory_Context memory_context = get_memory_context();
    String temp = copy_string(str, memory_context.temp);
    return (F32)atof(temp.data);
}