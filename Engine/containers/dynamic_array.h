#pragma once

#include "core/defines.h"
#include "core/memory.h"
#include "containers/array.h"

#define HE_DEFAULT_DYNAMIC_ARRAY_INITIAL_CAPACITY 16

template< typename T >
struct Dynamic_Array
{
    T *data;
    U32 count;
    U32 capacity;
    Allocator allocator;

    HE_FORCE_INLINE T& operator[](U32 index)
    {
        HE_ASSERT(this);
        HE_ASSERT(index < count);
        return data[index];
    }

    HE_FORCE_INLINE const T& operator[](U32 index) const
    {
        HE_ASSERT(this);
        HE_ASSERT(index < count);
        return data[index];
    }

    HE_FORCE_INLINE T* begin()
    {
        return &data[0];
    }

    HE_FORCE_INLINE T* end()
    {
        return &data[count];
    }

    HE_FORCE_INLINE const T* begin() const
    {
        return &data[0];
    }

    HE_FORCE_INLINE const T* end() const
    {
        return &data[count];
    }
};

template< typename T >
void init(Dynamic_Array< T > *dynamic_array, Allocator allocator, U32 initial_count = 0, U32 initial_capacity = 0)
{
    HE_ASSERT(dynamic_array);

    if (!initial_capacity)
    {
        initial_capacity = initial_count ? initial_count * 2 : HE_DEFAULT_DYNAMIC_ARRAY_INITIAL_CAPACITY;
    }

    dynamic_array->data = nullptr;

    std::visit([&](auto &&allocator)
    {
        dynamic_array->data = HE_ALLOCATE_ARRAY(allocator, T, initial_capacity);
    }, allocator);

    dynamic_array->count = initial_count;
    dynamic_array->capacity = initial_capacity;
    dynamic_array->allocator = allocator;
}

template< typename T >
void deinit(Dynamic_Array< T > *dynamic_array)
{
    HE_ASSERT(dynamic_array);
    HE_ASSERT(dynamic_array->data);

    std::visit([&](auto &&allocator)
    {
       deallocate(allocator, dynamic_array->data);
    }, dynamic_array->allocator);
    
    dynamic_array->data = nullptr;
    dynamic_array->count = 0;
    dynamic_array->capacity = 0;
}

template< typename T >
void set_capacity(Dynamic_Array< T > *dynamic_array, U32 new_capacity)
{
    HE_ASSERT(dynamic_array);

    if (new_capacity < dynamic_array->capacity * 2)
    {
        new_capacity = dynamic_array->capacity ? dynamic_array->capacity * 2 : HE_DEFAULT_DYNAMIC_ARRAY_INITIAL_CAPACITY;
    }

    std::visit([&](auto &&allocator)
    {
       dynamic_array->data = HE_REALLOCATE_ARRAY(allocator, dynamic_array->data, T, new_capacity);
    }, dynamic_array->allocator);

    dynamic_array->capacity = new_capacity;
}

template< typename T >
void set_count(Dynamic_Array< T > *dynamic_array, U32 new_count)
{
    HE_ASSERT(dynamic_array);

    if (new_count > dynamic_array->capacity)
    {
        set_capacity(dynamic_array, new_count);
    }

    dynamic_array->count = new_count;
}

template< typename T >
void reset(Dynamic_Array< T > *dynamic_array)
{
    HE_ASSERT(dynamic_array);
    dynamic_array->count = 0;
}

template< typename T >
void append(Dynamic_Array< T > *dynamic_array, const T &datum)
{
    HE_ASSERT(dynamic_array);

    if (dynamic_array->count == dynamic_array->capacity)
    {
        set_capacity(dynamic_array, dynamic_array->count * 2);
    }
    dynamic_array->data[dynamic_array->count++] = datum;
}

template< typename T >
T& append(Dynamic_Array< T > *dynamic_array)
{
    HE_ASSERT(dynamic_array);

    if (dynamic_array->count == dynamic_array->capacity)
    {
        set_capacity(dynamic_array, dynamic_array->count * 2);
    }
    return dynamic_array->data[dynamic_array->count++];
}

template< typename T >
U32 index_of(Dynamic_Array< T > *dynamic_array, T &datum)
{
    HE_ASSERT(dynamic_array);
    U64 index = &datum - dynamic_array->data;
    HE_ASSERT(index >= 0 && index < dynamic_array->count);
    return (U32)index;
}

template< typename T >
U32 index_of(Dynamic_Array< T > *dynamic_array, T *datum)
{
    HE_ASSERT(dynamic_array);
    U64 index = datum - dynamic_array->data;
    HE_ASSERT(index >= 0 && index < dynamic_array->count);
    return (U32)index;
}

template< typename T >
void remove_back(Dynamic_Array< T > *dynamic_array)
{
    HE_ASSERT(dynamic_array);
    HE_ASSERT(dynamic_array->count);
    dynamic_array->count--;
}

template< typename T >
void remove_and_swap_back(Dynamic_Array< T > *dynamic_array, U32 index)
{
    HE_ASSERT(dynamic_array);
    HE_ASSERT(index < dynamic_array->count);
    dynamic_array->data[index] = dynamic_array->data[dynamic_array->count - 1];
    dynamic_array->count--;
}

template< typename T >
void remove_ordered(Dynamic_Array< T > *dynamic_array, U32 index)
{
    HE_ASSERT(dynamic_array);
    HE_ASSERT(index < dynamic_array->count);
    for (U32 i = index; i < dynamic_array->count - 1; i++)
    {
        dynamic_array->data[i] = dynamic_array->data[i + 1];
    }
    dynamic_array->count--;
}

template< typename T >
HE_FORCE_INLINE T& front(Dynamic_Array< T > *dynamic_array)
{
    HE_ASSERT(dynamic_array);
    HE_ASSERT(dynamic_array->count);
    return dynamic_array->data[0];
}

template< typename T >
HE_FORCE_INLINE T& back(Dynamic_Array< T > *dynamic_array)
{
    HE_ASSERT(dynamic_array);
    HE_ASSERT(dynamic_array->count);
    return dynamic_array->data[dynamic_array->count - 1];
}

template< typename T >
S32 find(const Dynamic_Array< T > *dynamic_array, const T &target)
{
    HE_ASSERT(dynamic_array);

    for (S32 index = 0; index < (S32)dynamic_array->count; index++)
    {
        if (dynamic_array->data[index] == target)
        {
            return index;
        }
    }

    return -1;
}

template< typename T >
HE_FORCE_INLINE U64 size_in_bytes(const Dynamic_Array< T > *dynamic_array)
{
    HE_ASSERT(dynamic_array);
    return sizeof(T) * dynamic_array->count;
}

template< typename T >
HE_FORCE_INLINE U64 capacity_in_bytes(const Dynamic_Array< T > *dynamic_array)
{
    HE_ASSERT(dynamic_array);
    return sizeof(T) * dynamic_array->capacity;
}

template< typename T >
HE_FORCE_INLINE Array_View< T > to_array_view(const Dynamic_Array< T > &array)
{
    return { array.count, array.data };
}