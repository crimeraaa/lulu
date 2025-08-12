#pragma once

#include "slice.hpp"

template<class T, isize N>
struct Small_Array {
    Array<T, N> data;
    isize       len;
};

template<class T, auto N>
constexpr void
small_array_clear(Small_Array<T, N> *sa)
{
    sa->len = 0;
}

template<class T, auto N>
inline void
small_array_resize(Small_Array<T, N> *sa, isize n)
{
    lulu_assert(0 <= n && n <= N);
    sa->len = n;
}

template<class T, auto N>
inline void
small_array_push(Small_Array<T, N> *sa, const T &v)
{
    sa->data[sa->len++] = v;
}

template<class T, auto N>
inline void
small_array_pop(Small_Array<T, N> *sa)
{
    sa->len--;
}

template<class T, isize N>
constexpr isize
small_array_len(const Small_Array<T, N> &sa)
{
    return sa.len;
}

template<class T, isize N>
constexpr isize
small_array_cap(const Small_Array<T, N> &sa)
{
    unused(sa);
    return N;
}

template<class T, isize N>
inline T
small_array_get(const Small_Array<T, N> &sa, isize i)
{
    return sa.data[i];
}

template<class T, auto N>
inline T *
small_array_get_ptr(Small_Array<T, N> *sa, isize i)
{
    return &sa->data[i];
}

template<class T, isize N>
inline Slice<T>
small_array_slice(Small_Array<T, N> &sa)
{
    return slice(sa.data, 0, sa.len);
}
