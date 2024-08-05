#pragma once

#include "core/defines.h"
#include "core/memory.h"

#include "containers/array_view.h"

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
Dynamic_Array< T > make_dynamic_array(Allocator allocator)
{
    HE_ASSERT(allocator.data);
    Dynamic_Array< T > result = {};
    result.allocator = allocator;
    return result;
}

template< typename T >
void deinit(Dynamic_Array< T > *dynamic_array)
{
    HE_ASSERT(dynamic_array);
    
    if (dynamic_array->allocator.data)
    {
        HE_ALLOCATOR_DEALLOCATE(dynamic_array->allocator, dynamic_array->data);
        dynamic_array->data = nullptr;
    }
}

template< typename T >
void set_capacity(Dynamic_Array< T > *dynamic_array, U32 new_capacity)
{
    HE_ASSERT(dynamic_array);
    
    if (!new_capacity)
    {
        new_capacity = HE_DEFAULT_DYNAMIC_ARRAY_INITIAL_CAPACITY;
    }

    if (!dynamic_array->allocator.data)
    {
        Memory_Context memory_context = grab_memory_context();
        dynamic_array->allocator = memory_context.general_allocator;
    }

    dynamic_array->data = HE_ALLOCATOR_REALLOCATE_ARRAY_SIZED(dynamic_array->allocator, dynamic_array->data, T, dynamic_array->capacity, new_capacity);
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
void append(Dynamic_Array< T > *dynamic_array, const T &item)
{
    HE_ASSERT(dynamic_array);

    if (dynamic_array->count == dynamic_array->capacity)
    {
        set_capacity(dynamic_array, dynamic_array->count * 2);
    }
    dynamic_array->data[dynamic_array->count++] = item;
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
U32 index_of(Dynamic_Array< T > *dynamic_array, const T &item)
{
    HE_ASSERT(dynamic_array);
    S64 index = &item - dynamic_array->data;
    HE_ASSERT(index >= 0 && index < dynamic_array->count);
    return (U32)index;
}

template< typename T >
U32 index_of(Dynamic_Array< T > *dynamic_array, const T *item)
{
    HE_ASSERT(dynamic_array);
    S64 index = item - dynamic_array->data;
    HE_ASSERT(index >= 0 && index < dynamic_array->count);
    return (U32)index;
}

template< typename T >
void remove_back(Dynamic_Array< T > *dynamic_array)
{
    HE_ASSERT(dynamic_array);
    HE_ASSERT(dynamic_array->count > 0);
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
HE_FORCE_INLINE U64 get_size_in_bytes(const Dynamic_Array< T > *dynamic_array)
{
    HE_ASSERT(dynamic_array);
    return sizeof(T) * dynamic_array->count;
}

template< typename T >
HE_FORCE_INLINE U64 get_capacity_in_bytes(const Dynamic_Array< T > *dynamic_array)
{
    HE_ASSERT(dynamic_array);
    return sizeof(T) * dynamic_array->capacity;
}

template< typename T >
HE_FORCE_INLINE Array_View< T > to_array_view(const Dynamic_Array< T > &array)
{
    return { array.count, array.data };
}