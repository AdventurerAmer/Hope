#pragma once

#include "core/defines.h"
#include "core/memory.h"

struct String
{
    const char *data;
    U64 count;
};

template< U64 Count >
constexpr U64 compile_time_string_length(const char (&)[Count])
{
    return Count - 1;
}

template< U64 Count >
constexpr U64 compile_time_string_hash(const char (&str)[Count])
{
    const U64 p = 31;
    const U64 m = U64(1e9) + 7;

    U64 hash = 0;
    U64 multiplier = 1;
    for (U32 i = 0; i < Count - 1; i++)
    {
        hash = ((hash + (str[i] - 'a' + 1) * multiplier)) % m;
        multiplier = (multiplier * p) % m;
    }
    return hash;
}

#define HOPE_String(AnciiStringLiteral) { AnciiStringLiteral, compile_time_string_length(AnciiStringLiteral) }
#define HOPE_ExpandString(StringPointer) (U32)((StringPointer)->count), (StringPointer)->data

U64 string_length(const char *str);

U64 hash(const String *str);

template< typename Allocator >
String copy_string(const char *str, U64 count, Allocator *allocator)
{
    if (!count)
    {
        return HOPE_String("");
    }
    char *data = AllocateArray(allocator, char, count + 1);
    copy_memory(data, str, count);
    data[count] = 0;
    return { data, count };
}

bool equal(const char *a, const char *b);
bool equal(const String *a, const String *b);

HOPE_FORCE_INLINE bool equal(const String *a, const char *b, U64 count)
{
    String str = { b, count };
    return equal(a, &str);
}

HOPE_FORCE_INLINE bool equal(const String *a, const char *b)
{
    String str = { b, string_length(b) };
    return equal(a, &str);
}

S64 find_first_char_from_left(const String *str, const char *chars);
S64 find_first_char_from_right(const String *str, const char *chars);

String sub_string(const String *str, U64 index);
String sub_string(const String *str, U64 index, U64 count);

String format_string(struct Memory_Arena *arena, const char *format, ...);
String format_string(struct Memory_Arena *arena, const char *format, va_list args);

struct String_Builder
{
    Memory_Arena *arena;
    U64 max_count;

    char *data;
    U64 count;
};

void begin_string_builder(String_Builder *string_builder, Memory_Arena *arena);
void push(String_Builder *string_builder, const char *format, ...);
String end_string_builder(String_Builder *string_builder);