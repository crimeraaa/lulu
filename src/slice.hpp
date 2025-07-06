#pragma once

#include <string.h> // memcmp, memmove

#include "array.hpp"

template<class T>
struct Slice {
    using size_type = size_t;

    T        *data;
    size_type len;

    // Bounds-checked, mutable element access.
    template<class N>
    T &
    operator[](N i)
    {
        size_type ii = cast(size_type)i;
        lulu_assertf(ii < this->len, "Out of bounds index %zu / %zu", ii, this->len);
        return this->data[ii];
    }

    // Bounds-checked, non-mutable element access.
    template<class N>
    const T &
    operator[](N i) const
    {
        size_type ii = cast(size_type)i;
        lulu_assertf(ii < this->len, "Out of bounds index %zu / %zu", ii, this->len);
        return this->data[ii];
    }
};

template<class T>
LULU_FUNC inline Slice<T>
slice_pointer(T *start, T *stop)
{
    Slice<T> s{start, cast(typename Slice<T>::size_type)(stop - start)};
    // Problem: comparison of 2 unrelated pointers is not standard!
    lulu_assertf(start <= stop, "start=%p > stop=%p", cast(void *)start, cast(void *)stop);
    return s;
}

template<class T>
LULU_FUNC inline Slice<T>
slice_slice(Slice<T> &s, typename Slice<T>::size_type start, typename Slice<T>::size_type stop)
{
    Slice<T> s2{&s.data[start], stop - start};
    lulu_assertf(len(s2) <= len(s), "len(s2)=%zu > len(s)=%zu", len(s2), len(s));
    lulu_assertf(start <= len(s), "start=%zu > %zu", start, len(s));
    lulu_assertf(stop <= len(s), "stop=%zu > %zu", stop, len(s));
    lulu_assertf(start <= stop, "start=%zu > stop=%zu", start, stop);
    return s2;
}

template<class T, auto N>
LULU_FUNC inline Slice<T>
slice_array(Array<T, N> &a, typename Slice<T>::size_type start, typename Slice<T>::size_type stop)
{
    Slice<T> s{&a.data[start], stop - start};
    lulu_assertf(len(s) <= N, "len(s)=%zu > N=%zu", len(s), N);
    lulu_assertf(start <= N, "start=%zu > N=%zu", start, N);
    lulu_assertf(stop <= N, "stop=%zu > N=%zu", stop, N);
    lulu_assertf(start <= stop, "start=%zu > stop=%zu", start, stop);
    return s;
}

template<class T>
LULU_FUNC inline bool
slice_eq(Slice<T> a, Slice<T> b)
{
    if (len(a) != len(b)) {
        return false;
    }
    return memcmp(raw_data(a), raw_data(b), sizeof(T) * len(a)) == 0;
}

template<class T>
LULU_FUNC constexpr typename Slice<T>::size_type
len(Slice<T> s)
{
    return s.len;
}

template<class T>
LULU_FUNC constexpr T *
raw_data(Slice<T> s)
{
    return s.data;
}

template<class T>
LULU_FUNC inline void
copy(Slice<T> dst, Slice<T> src)
{
    // Clamp size to read and copy
    typename Slice<T>::size_type n = (dst.len > src.len) ? src.len : dst.len;
    memmove(dst.data, src.data, sizeof(T) * n);
}

// Because C++ templates aren't convoluted enough
template<class T>
LULU_FUNC inline void
copy(Slice<T> dst, Slice<const T> src)
{
    // Clamp size to read and copy
    typename Slice<T>::size_type n = (dst.len > src.len) ? src.len : dst.len;
    memmove(dst.data, src.data, sizeof(T) * n);
}

template<class T>
LULU_FUNC constexpr void
fill(Slice<T> s, T init)
{
    for (T &v : s) {
        v = init;
    }
}

// Mutable forward iterator initial value.
template<class T>
LULU_FUNC constexpr T *
begin(Slice<T> s)
{
    return s.data;
}

// Mutable forward iterator final value.
template<class T>
LULU_FUNC constexpr T *
end(Slice<T> s)
{
    return s.data + s.len;
}
