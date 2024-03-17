#pragma once

#include "core/defines.h"
#include "containers/array_view.h"

#include <initializer_list>

template< typename T, U32 N >
struct Counted_Array
{
    T data[N];
    U32 count = 0;

    Counted_Array() = default;

    Counted_Array(const std::initializer_list< T >& items)
    {
        count = 0;
        
        for (const T &item : items)
        {
            data[count++] = item;
        }
    }

    Counted_Array< T, N >& operator=(const std::initializer_list< T > &items)
    {
        count = 0;
        
        for (const T &item : items)
        {
            data[count++] = item;
        }

        return *this;
    }

    HE_FORCE_INLINE T& operator[](U32 index)
    {
        HE_ASSERT(index >= 0 && index < N);
        return data[index];
    }

    HE_FORCE_INLINE const T& operator[](U32 index) const
    {
        HE_ASSERT(index >= 0 && index < N);
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

template< typename T, U32 N >
void set_count(Counted_Array< T, N > *array, U32 count)
{
    HE_ASSERT(array);
    HE_ASSERT(count <= N);
    array->count = count;
}

template< typename T, U32 N >
void reset(Counted_Array< T, N > *array)
{
    HE_ASSERT(array);
    array->count = 0;
}

template< typename T, U32 N >
void append(Counted_Array< T, N > *array, const T &item)
{
    HE_ASSERT(array);
    HE_ASSERT(array->count < N);
    array->data[array->count++] = item;
}

template< typename T, U32 N >
T& append(Counted_Array< T, N > *array)
{
    HE_ASSERT(array);
    HE_ASSERT(array->count < N);
    return array->data[array->count++];
}

template< typename T, U32 N >
U32 index_of(Counted_Array< T, N > *array, T &item)
{
    HE_ASSERT(array);
    S64 index = &item - array->data;
    HE_ASSERT(index >= 0 && index < array->count);
    return (U32)index;
}

template< typename T, U32 N >
U32 index_of(Counted_Array< T, N > *array, T *item)
{
    HE_ASSERT(array);
    S64 index = item - array->data;
    HE_ASSERT(index >= 0 && index < array->count);
    return (U32)index;
}

template< typename T, U32 N >
void remove_back(Counted_Array< T, N > *array)
{
    HE_ASSERT(array);
    HE_ASSERT(array->count);
    array->count--;
}

template< typename T, U32 N >
void remove_and_swap_back(Counted_Array< T, N > *array, U32 index)
{
    HE_ASSERT(array);
    HE_ASSERT(index < array->count);
    if (index != array->count - 1)
    {
        array->data[index] = array->data[array->count - 1];
    }
    array->count--;
}

template< typename T, U32 N >
void remove_ordered(Counted_Array< T, N > *array, U32 index)
{
    HE_ASSERT(array);
    HE_ASSERT(index < array->count);
    for (U32 i = index; i < array->count - 1; i++)
    {
        array->data[i] = array->data[i + 1];
    }
    array->count--;
}

template< typename T, U32 N >
HE_FORCE_INLINE T& front(Counted_Array< T, N > *array)
{
    HE_ASSERT(array);
    HE_ASSERT(array->count);
    return array->data[0];
}

template< typename T, U32 N >
HE_FORCE_INLINE T& back(Counted_Array< T, N > *array)
{
    HE_ASSERT(array);
    HE_ASSERT(array->count);
    return array->data[array->count - 1];
}

template< typename T, U32 N >
S32 find(const Counted_Array< T, N > *array, const T &target)
{
    HE_ASSERT(array);

    for (S32 index = 0; index < (S32)array->count; index++)
    {
        if (array->data[index] == target)
        {
            return index;
        }
    }

    return -1;
}

template< typename T, U32 N >
constexpr U32 capacity(Counted_Array< T, N > *array)
{
    HE_ASSERT(array);
    return N;
}

template< typename T, U32 N >
void copy(Counted_Array< T, N > *dst, const Counted_Array< T, N > *src)
{
    copy_memory(dst->data, src->data, sizeof(T) * src->count);
    dst->count = src->count;
}

template< typename T, U32 N >
void copy(Counted_Array< T, N > *array, const Array_View< T > array_view)
{
    HE_ASSERT(array_view.count <= N);
    copy_memory(array->data, array_view.data, sizeof(T) * array_view.count);
    array->count = array_view.count;
}

template< typename T, U32 N >
HE_FORCE_INLINE U64 size_in_bytes(const Counted_Array< T, N > *array)
{
    HE_ASSERT(array);
    return sizeof(T) * array->count;
}

template< typename T, U32 N >
HE_FORCE_INLINE U64 capacity_in_bytes(const Counted_Array< T, N > *array)
{
    HE_ASSERT(array);
    return sizeof(T) * array->capacity;
}

template< typename T, U32 N >
HE_FORCE_INLINE Array_View< T > to_array_view(const Counted_Array< T, N > &array)
{
    return { array.count, array.data };
}