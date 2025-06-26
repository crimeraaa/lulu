#pragma once

#include "private.hpp"

template<class T, size_t N>
struct Array {
    using value_type      = T;
    using size_type       = size_t;
    using pointer         = value_type *;
    using reference       = value_type &;
    using const_pointer   = const value_type *;
    using const_reference = const value_type &;

    value_type data[N];

    reference
    operator[](size_type i)
    {
        lulu_assertf(i < N, "Out of bounds index %zu / %zu", i, N);
        return this->data[i];
    }

    const_reference
    operator[](size_type i) const
    {
        lulu_assertf(i < N, "Out of bounds index %zu / %zu", i, N);
        return this->data[i];
    }
};

template<class T, size_t N>
inline typename Array<T, N>::size_type
len(const Array<T, N> &a)
{
    unused(a);
    return N;
}

template<class T, size_t N>
inline typename Array<T, N>::pointer
raw_data(Array<T, N> &a)
{
    return a.data;
}

template<class T, size_t N>
struct Small_Array : public Array<T, N> {
    size_t len;
};

template<class T, size_t N>
constexpr typename Small_Array<T, N>::size_type
len(const Small_Array<T, N> &sa)
{
    return sa.len;
}

template<class T, size_t N>
constexpr typename Small_Array<T, N>::size_type
cap(const Small_Array<T, N> &sa)
{
    unused(sa);
    return N;
}

template<class T, size_t N>
inline typename Small_Array<T, N>::reference
small_array_next(Small_Array<T, N> &sa)
{
    return sa[sa.len++];
}

template<class T, size_t N>
inline typename Small_Array<T, N>::reference
small_array_prev(Small_Array<T, N> &sa)
{
    return sa[--sa.len];
}
