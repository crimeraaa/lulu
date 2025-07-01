#pragma once

#include "private.hpp"

template<class T, size_t N>
struct Array {
    using size_type = size_t;

    T data[N];

    T &
    operator[](size_type i)
    {
        lulu_assertf(i < N, "Out of bounds index %zu / %zu", i, N);
        return this->data[i];
    }

    const T &
    operator[](size_type i) const
    {
        lulu_assertf(i < N, "Out of bounds index %zu / %zu", i, N);
        return this->data[i];
    }
};

template<class T, size_t N>
LULU_FUNC inline typename Array<T, N>::size_type
len(const Array<T, N> &a)
{
    unused(a);
    return N;
}

template<class T, size_t N>
LULU_FUNC inline T *
raw_data(Array<T, N> &a)
{
    return a.data;
}

template<class T, size_t N>
struct Small_Array : public Array<T, N> {
    size_t len;
};

template<class T, size_t N>
LULU_FUNC constexpr typename Array<T, N>::size_type
len(const Small_Array<T, N> &sa)
{
    return sa.len;
}

template<class T, size_t N>
LULU_FUNC constexpr typename Array<T, N>::size_type
cap(const Small_Array<T, N> &sa)
{
    unused(sa);
    return N;
}

template<class T, size_t N>
LULU_FUNC inline T &
small_array_push(Small_Array<T, N> &sa)
{
    return sa[sa.len++];
}

template<class T, size_t N>
LULU_FUNC inline void
small_array_pop(Small_Array<T, N> &sa)
{
    sa.len--;
}
