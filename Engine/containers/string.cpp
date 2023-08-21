#include "string.h"
#include "core/memory.h"

U64 string_length(const char *str)
{
    U64 length = 0;
    while (str[length])
    {
        length++;
    }
    return length;
}

bool equal(const String *a, const String *b)
{
    if (a->count != b->count)
    {
        return false;
    }

    // todo(amer): SIMD
    for (U32 char_index = 0; char_index < a->count; char_index++)
    {
        if (a->data[char_index] != b->data[char_index])
        {
            return false;
        }
    }

    return true;
}