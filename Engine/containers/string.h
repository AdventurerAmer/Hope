#pragma once

#include "core/defines.h"
#include "core/memory.h"

struct String
{
    U64 count;
    const char *data;
};

#define HE_STRING(string) String { string_length(string), string }
#define HE_STRING_LITERAL(string_literal) String { comptime_string_length(string_literal), string_literal }
#define HE_EXPAND_STRING(string) (U32)((string).count), (string).data

U64 string_length(const char *str);
U64 hash_key(String str);

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

String copy_string(const char *str, U64 count, Allocator allocator = {});

HE_FORCE_INLINE String copy_string(String str, Allocator allocator = {})
{
    return copy_string(str.data, str.count, allocator);
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

S64 find_first_char_from_left(String str, String chars, U64 offset = 0);
S64 find_first_char_from_right(String str, String chars);

bool starts_with(String str, String start);
bool ends_with(String str, String end);

String sub_string(String str, U64 index);
String sub_string(String str, U64 index, U64 count);

String format_string(Allocator allocator, const char *format, ...);
String format_string(Allocator allocator, const char *format, va_list args);

String advance(String str, U64 count);
String eat_chars(String str, String chars);
String eat_none_of_chars(String str, String chars);

String eat_white_space(String str);
String eat_none_white_space(String *str);

bool contains(String a, String b);

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

struct Parse_Name_Value_Result
{
    bool success;
    String value;
};

Parse_Name_Value_Result parse_name_value(String *str, String name);

struct Parse_Name_Float3_Result
{
    bool success;
    F32 values[3];
};

Parse_Name_Float3_Result parse_name_float3(String *str, String name);

U64 str_to_u64(String str);
S64 str_to_s64(String str);
F32 str_to_f32(String str);