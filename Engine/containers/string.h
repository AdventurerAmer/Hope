#pragma once

#include "core/defines.h"

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

#define HOPE_String(AnciiStringLiteral) { AnciiStringLiteral, compile_time_string_length(AnciiStringLiteral) }
#define HOPE_ExpandString(StringPointer) (U32)((StringPointer)->count), (StringPointer)->data

U64 string_length(const char *str);

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