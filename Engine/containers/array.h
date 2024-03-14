#pragma once

#include "core/defines.h"
#include "containers/array_view.h"

template< typename T, U32 N >
struct Array
{
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

    HE_FORCE_INLINE T* begin()
    {
        return &data[0];
    }

    HE_FORCE_INLINE T* end()
    {
        return &data[N];
    }

    HE_FORCE_INLINE const T* begin() const
    {
        return &data[0];
    }

    HE_FORCE_INLINE const T* end() const
    {
        return &data[N];
    }
};

template< typename T, U32 N >
HE_FORCE_INLINE T& front(Array< T, N > *array)
{
    HE_ASSERT(array);
    HE_ASSERT(array->count);
    return array->data[0];
}

template< typename T, U32 N >
HE_FORCE_INLINE T& back(Array< T, N > *array)
{
    HE_ASSERT(array);
    HE_ASSERT(array->count);
    return array->data[N - 1];
}

template< typename T, U32 N >
S32 find(const Array< T, N > *array, const T &target)
{
    HE_ASSERT(array);

    for (U32 index = 0; index < N; index++)
    {
        if (array->data[index] == target)
        {
            return (S32)index;
        }
    }

    return -1;
}

template< typename T, U32 N >
HE_FORCE_INLINE constexpr U32 count(Array< T, N > *array)
{
    HE_ASSERT(array);
    return N;
}

template< typename T, U32 N >
void copy(Array< T, N > *dst, const Array< T, N > *src)
{
    copy_memory(dst->data, src->data, sizeof(T) * N);
}

template< typename T, U32 N >
HE_FORCE_INLINE Array_View< T > to_array_view(const Array< T, N > &array)
{
    return { array.count, array.data };
}