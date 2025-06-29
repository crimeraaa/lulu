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

    Slice()
        : data{nullptr}
        , len{0}
    {}

    Slice(T *data, size_type len)
        : data{data}
        , len{len}
    {}

    Slice(T *start, T *stop)
        : data{start}
        , len{cast_size(stop - start)}
    {}

    Slice(T *p, size_type start, size_type stop)
        : data{p + start}
        , len{stop - start}
    {}

    Slice(Slice<T> s, size_type start, size_type stop)
        : data{&s[start]}
        , len{stop - start}
    {}

    template<size_t N>
    Slice(Array<T, N> &s, size_type start, size_type stop)
        : data{&s[start]}
        , len{stop - start}
    {}

    template<size_t N>
    Slice(Array<T, N> &s)
        : data{&s[0]}
        , len{N}
    {}
};

template<class T>
inline bool
slice_eq(Slice<T> a, Slice<T> b)
{
    if (len(a) != len(b)) {
        return false;
    }
    return memcmp(raw_data(a), raw_data(b), sizeof(T) * len(a)) == 0;
}

template<class T>
inline typename Slice<T>::size_type
len(Slice<T> s)
{
    return s.len;
}

template<class T>
inline T *
raw_data(Slice<T> s)
{
    return s.data;
}

template<class T>
inline void
copy(Slice<T> dst, Slice<T> src)
{
    // Clamp size to read and copy
    typename Slice<T>::size_type n = (dst.len > src.len) ? src.len : dst.len;
    memmove(dst.data, src.data, sizeof(T) * n);
}

// Because C++ templates aren't convoluted enough
template<class T>
inline void
copy(Slice<T> dst, Slice<const T> src)
{
    // Clamp size to read and copy
    typename Slice<T>::size_type n = (dst.len > src.len) ? src.len : dst.len;
    memmove(dst.data, src.data, sizeof(T) * n);
}

// Mutable forward iterator initial value.
template<class T>
inline T *
begin(Slice<T> s)
{
    return s.data;
}

// Mutable forward iterator final value.
template<class T>
inline T *
end(Slice<T> s)
{
    return s.data + s.len;
}
