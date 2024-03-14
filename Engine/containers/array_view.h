#pragma once

template< typename T >
struct Array_View
{
    const U32 count;
    const T *data;

    HE_FORCE_INLINE const T& operator[](U32 index) const
    {
        HE_ASSERT(index >= 0 && index < count);
        return data[index];
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
HE_FORCE_INLINE Array_View< T > to_array_view(const T (&array)[N])
{
    return { N, array };
}