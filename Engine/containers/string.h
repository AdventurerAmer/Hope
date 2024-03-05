#pragma once

#include "core/defines.h"
#include "core/memory.h"

struct String
{
    const char *data;
    U64 count;
};

#define HE_STRING(string) { string, string_length(string) }
#define HE_STRING_LITERAL(string_literal) { string_literal, comptime_string_length(string_literal) }
#define HE_EXPAND_STRING(string) (U32)((string).count), (string).data

U64 string_length(const char *str);
U64 hash_key(const String &str);

template< U64 Count >
constexpr U64 comptime_string_length(const char(&)[Count])
{
    return Count - 1;
}

template< U64 Count >
constexpr U64 comptime_string_hash(const char(&str)[Count])
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

String copy_string(const char *str, U64 count, auto allocator)
{
    if (!count)
    {
        return HE_STRING_LITERAL("");
    }

    char *data = HE_ALLOCATE_ARRAY(allocator, char, count + 1);
    copy_memory(data, str, count);
    data[count] = 0;
    return { data, count };
}

String copy_string(const String &string, auto allocator)
{
    return copy_string(string.data, string.count, allocator);
}

bool equal(const char *a, U64 a_length, const char *b, U64 b_length);

HE_FORCE_INLINE bool operator==(const String &lhs, const String &rhs)
{
    return equal(lhs.data, lhs.count, rhs.data, rhs.count);
}

HE_FORCE_INLINE bool operator==(const String &lhs, const char *rhs)
{
    U64 rhs_length = string_length(rhs);
    return equal(lhs.data, lhs.count, rhs, rhs_length);
}

HE_FORCE_INLINE bool operator==(const char *lhs, const String &rhs)
{
    U64 lhs_length = string_length(lhs);
    return equal(lhs, lhs_length, rhs.data, rhs.count);
}

HE_FORCE_INLINE bool operator!=(const String &lhs, const String &rhs)
{
    return !equal(lhs.data, lhs.count, rhs.data, rhs.count);
}

HE_FORCE_INLINE bool operator!=(const String &lhs, const char *rhs)
{
    U64 rhs_length = string_length(rhs);
    return !equal(lhs.data, lhs.count, rhs, rhs_length);
}

HE_FORCE_INLINE bool operator!=(const char *lhs, const String &rhs)
{
    U64 lhs_length = string_length(lhs);
    return !equal(lhs, lhs_length, rhs.data, rhs.count);
}

S64 find_first_char_from_left(const String &str, const String &chars, U64 offset = 0);
S64 find_first_char_from_right(const String &str, const String &chars);

bool starts_with(const String &str, const String &start);
bool ends_with(const String &str, const String &end);

String sub_string(const String &str, U64 index);
String sub_string(const String &str, U64 index, U64 count);

String format_string(struct Memory_Arena *arena, const char *format, ...);
String format_string(struct Memory_Arena *arena, const char *format, va_list args);

String advance(const String &str, U64 count);
String eat_chars(const String &str, const String &chars);

struct String_Builder
{
    Memory_Arena *arena;
    U64 max_count;

    char *data;
    U64 count;
};

void begin_string_builder(String_Builder *string_builder, Memory_Arena *arena);
void append(String_Builder *string_builder, const char *format, ...);
String end_string_builder(String_Builder *string_builder);