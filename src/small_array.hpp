#pragma once

#include "array.hpp"
#include "slice.hpp"

template<class T, size_t N>
struct Small_Array {
    Array<T, N> data;
    size_t      len;
};

template<class T, size_t N>
LULU_FUNC constexpr typename Array<T, N>::size_type
small_array_len(const Small_Array<T, N> &sa)
{
    return sa.len;
}

template<class T, size_t N>
LULU_FUNC constexpr typename Array<T, N>::size_type
small_array_cap(const Small_Array<T, N> &sa)
{
    unused(sa);
    return N;
}

template<class T, size_t N>
LULU_FUNC Slice<T>
small_array_slice(Small_Array<T, N> &sa)
{
    return slice_array(sa.data, 0, sa.len);
}

template<class T, size_t N>
LULU_FUNC T
small_array_get(const Small_Array<T, N> &sa, typename Array<T, N>::size_type index)
{
    return sa.data[index];
}

template<class T, size_t N>
LULU_FUNC T *
small_array_get_ptr(Small_Array<T, N> *sa, typename Array<T, N>::size_type index)
{
    return &sa->data[index];
}

template<class T, size_t N>
LULU_FUNC inline void
small_array_push(Small_Array<T, N> *sa, T value)
{
    sa->data[sa->len++] = value;
}

template<class T, size_t N>
LULU_FUNC constexpr void
small_array_pop(Small_Array<T, N> *sa)
{
    sa->len--;
}

template<class T, size_t N>
LULU_FUNC constexpr void
small_array_resize(Small_Array<T, N> *sa, size_t n)
{
    sa->len = n;
}
