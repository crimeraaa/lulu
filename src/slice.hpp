#pragma once

#include <string.h> // memcmp, memmove

struct Raw_Slice {
    void  *data;
    size_t len;
};

template<class T>
struct Slice {
    T     *data;
    size_t len;

    // Bounds-checked, mutable element access.
    template<class N>
    T &operator[](N i)
    {
        size_t ii = cast_size(i);
        lulu_assertf(ii < this->len, "Out of bounds index %zu", ii);
        return this->data[ii];
    }

    // Bounds-checked, non-mutable element access.
    template<class N>
    const T &operator[](N i) const
    {
        size_t ii = cast_size(i);
        lulu_assertf(ii < this->len, "Out of bounds index %zu", ii);
        return this->data[ii];
    }

    Slice()
        : data{nullptr}
        , len{0}
    {}

    Slice(T *data, size_t len)
        : data{data}
        , len{len}
    {}

    Slice(T *start, T *stop)
        : data{start}
        , len{cast_size(stop - start)}
    {}

    Slice(T *p, size_t start, size_t stop)
        : data{p + start}
        , len{stop - start}
    {}

    Slice(Slice<T> s, size_t start, size_t stop)
        : data{&s[start]}
        , len{stop - start}
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
inline size_t
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
    const size_t n = (dst.len > src.len) ? src.len : dst.len;
    memmove(dst.data, src.data, sizeof(T) * n);
}

// Because C++ templates aren't convoluted enough
template<class T>
inline void
copy(Slice<T> dst, Slice<const T> src)
{
    // Clamp size to read and copy
    const size_t n = (dst.len > src.len) ? src.len : dst.len;
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
