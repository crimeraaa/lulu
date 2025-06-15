#pragma once

#include <string.h> // memmove

template<class T>
struct Slice {
    T     *data;
    size_t len;

    // Bounds-checked, mutable element access.
    template<class N>
    T &operator[](N i)
    {
        size_t ii = cast(size_t, i);
        lulu_assertf(ii < this->len, "Out of bounds index %zu", ii);
        return this->data[ii];
    }

    // Bounds-checked, non-mutable element access.
    template<class N>
    const T &operator[](N i) const
    {
        size_t ii = cast(size_t, i);
        lulu_assertf(ii < this->len, "Out of bounds index %zu", ii);
        return this->data[ii];
    }
};


template<class T>
inline Slice<T>
slice_make(T *data, size_t len)
{
    return {data, len};
}

template<class T>
inline Slice<T>
slice_make(Slice<T> s, size_t start, size_t stop)
{
    return {&s[start], stop - start};
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
