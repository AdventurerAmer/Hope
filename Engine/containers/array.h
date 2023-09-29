#pragma once

#include "core/defines.h"

template< typename T, const U32 N >
struct Array
{
    static constexpr U32 capacity = N;

    U32 count = 0;
    T data[N];

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

    Array< T, N >& operator=(const Array< T, N > &other) = delete;

    Array< T, N >& operator=(const std::initializer_list< T > &&items)
    {
        HE_ASSERT(items.size() <= N);
        count = 0;
        
        for (const T &item : items)
        {
            data[count++] = item;
        }

        return *this;
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

template< typename T, const U32 N >
void reset(Array< T, N > *array)
{
    HE_ASSERT(array);
    array->count = 0;
}

template< typename T, const U32 N >
void append(Array< T, N > *array, const T &datum)
{
    HE_ASSERT(array);
    HE_ASSERT(array->count < N);
    array->data[array->count++] = datum;
}

template< typename T, const U32 N >
void remove_back(Array< T, N > *array)
{
    HE_ASSERT(array);
    HE_ASSERT(array->count);
    array->count--;
}

template< typename T, const U32 N >
void remove_and_swap_back(Array< T, N > *array, U32 index)
{
    HE_ASSERT(array);
    HE_ASSERT(index < array->count);
    array->data[index] = array->data[array->count - 1];
    array->count--;
}

template< typename T, const U32 N >
void remove_ordered(Array< T, N > *array, U32 index)
{
    HE_ASSERT(array);
    HE_ASSERT(index < array->count);
    for (U32 i = index; i < array->count - 1; i++)
    {
        array->data[i] = array->data[i + 1];
    }
    array->count--;
}

template< typename T, const U32 N >
S32 find(const Array< T, N > *array, const T &target)
{
    HE_ASSERT(array);

    for (S32 index = 0; index < array->count; index++)
    {
        if (array->data[index] == target)
        {
            return it;
        }
    }

    return -1;
}

template< typename T, const U32 N >
HE_FORCE_INLINE Size size_in_bytes(const Array< T, N > *array)
{
    HE_ASSERT(array);
    return sizeof(T) * array->count;
}

template< typename T, const U32 N >
HE_FORCE_INLINE Size capacity_in_bytes(Array< T, N > *array)
{
    HE_ASSERT(array);
    return sizeof(T) * N;
}

template< typename T, const U32 N >
void copy(Array< T, N > &dst, const Array< T, N > &src)
{
    dst.count = src.count;
    copy_memory(dst.data, src.data, size_in_bytes(&src));
}