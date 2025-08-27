#pragma once

#include <string.h> // memcmp, memmove

#include "private.hpp"

//=== SLICE ================================================================ {{{

template<class T>
struct Slice {
    T    *data;
    isize len;

    // Bounds-checked, mutable element access.
    template<class N>
    T &
    operator[](N i)
    {
        isize ii = static_cast<isize>(i);
        lulu_assertf(0 <= ii && ii < this->len,
            "Out of bounds index %" ISIZE_FMT " / %" ISIZE_FMT,
            ii, this->len);
        return this->data[ii];
    }

    // Bounds-checked, non-mutable element access.
    template<class N>
    const T &
    operator[](N i) const
    {
        isize ii = static_cast<isize>(i);
        lulu_assertf(0 <= ii && ii < this->len,
            "Out of bounds index %" ISIZE_FMT " / %" ISIZE_FMT,
            ii, this->len);
        return this->data[ii];
    }
};

template<class T>
constexpr Slice<T>
slice(Slice<T> &s)
{
    return {raw_data(s), len(s)};
}

template<class T>
constexpr Slice<const T>
slice_const(const Slice<T> &s)
{
    return {raw_data(s), len(s)};
}

// Similar to `slice[:]` in Odin and `list[:]` in Python.
template<class T>
inline Slice<T>
slice(Slice<T> &s, isize start, isize stop)
{
    Slice<T> s2{&s.data[start], stop - start};
    lulu_assertf(
        0 <= len(s2) && len(s2) <= len(s),
        "invalid result length: len(s2)=%" ISIZE_FMT " > len(s)=%" ISIZE_FMT,
        len(s2),
        len(s)
    );

    lulu_assertf(
        0 <= start && start <= len(s),
        "invalid start index: start=%" ISIZE_FMT " > %" ISIZE_FMT,
        start,
        len(s)
    );

    lulu_assertf(
        0 <= stop && stop <= len(s),
        "invalid stop index: stop=%" ISIZE_FMT " > %" ISIZE_FMT,
        stop,
        len(s)
    );

    lulu_assertf(
        start <= stop,
        "invalid start-stop pair: start=%" ISIZE_FMT " > stop=%" ISIZE_FMT,
        start,
        stop
    );
    return s2;
}

// Similar to `slice[start:]` in Odin and `list[start:] in Python.
template<class T>
inline Slice<T>
slice_from(Slice<T> &s, isize start)
{
    return slice(s, start, len(s));
}

// R-value shenanigans.
template<class T>
inline Slice<T>
slice_from(Slice<T> &&s, isize start)
{
    return slice(s, start, len(s));
}

// Similar to `slice[:stop]` in Odin and `list[:stop]` in Python.
template<class T>
inline Slice<T>
slice_until(Slice<T> &s, isize stop)
{
    return slice(s, 0, stop);
}

template<class T>
inline bool
slice_eq(const Slice<T> &a, const Slice<T> &b)
{
    // Fast path: slices of differing lengths are never equal.
    if (a.len != b.len) {
        return false;
    }
    usize size = sizeof(T) * static_cast<usize>(a.len);
    return memcmp(a.data, b.data, size) == 0;
}

template<class T>
inline Slice<T>
slice_pointer(T *start, T *stop)
{
    Slice<T> s{start, static_cast<isize>(stop - start)};
    // Problem: comparison of 2 unrelated pointers is not standard!
    lulu_assertf(
        start <= stop,
        "start=%p > stop=%p",
        static_cast<const void *>(start),
        static_cast<const void *>(stop)
    );
    return s;
}

template<class T>
inline Slice<T>
slice_pointer_len(T *data, isize n)
{
    Slice<T> s{data, n};
    return s;
}

template<class T>
constexpr isize
len(const Slice<T> &s)
{
    return s.len;
}

// Mutable access to underlying slice data.
template<class T>
constexpr T *
raw_data(Slice<T> &s)
{
    return s.data;
}

// Read-only access to underlying slice data.
template<class T>
constexpr const T *
raw_data(const Slice<T> &s)
{
    return s.data;
}

template<class T>
inline void
copy(Slice<T> &dst, const Slice<T> &src)
{
    // Clamp size to read and copy
    isize n = (dst.len > src.len) ? src.len : dst.len;
    memmove(dst.data, src.data, sizeof(T) * static_cast<usize>(n));
}

// Because C++ templates aren't convoluted enough
template<class T>
inline void
copy(Slice<T> &dst, const Slice<const T> &src)
{
    // Clamp size to read and copy
    isize n = (dst.len > src.len) ? src.len : dst.len;
    memmove(dst.data, src.data, sizeof(T) * static_cast<usize>(n));
}

template<class T>
constexpr void
fill(Slice<T> &s, const T &init)
{
    for (T &slot : s) {
        slot = init;
    }
}

// R-value shenanigans.
template<class T>
constexpr void
fill(Slice<T> &&s, const T &init)
{
    for (T &slot : s) {
        slot = init;
    }
}

// Mutable forward iterator initial value.
template<class T>
constexpr T *
begin(Slice<T> &s)
{
    return s.data;
}

// Mutable forward iterator final value.
template<class T>
constexpr T *
end(Slice<T> &s)
{
    return s.data + s.len;
}

// Read-only forward iterator initial value.
template<class T>
constexpr const T *
begin(const Slice<T> &s)
{
    return s.data;
}

// Read-only forward iterator final value.
template<class T>
constexpr const T *
end(const Slice<T> &s)
{
    return s.data + s.len;
}

//=== }}} ======================================================================

//=== ARRAY ================================================================ {{{

template<class T, auto N>
struct Array {
    T data[N];

    template<class I>
    T &
    operator[](I i)
    {
        isize ii = static_cast<isize>(i);
        lulu_assertf(
            0 <= ii && ii < N,
            "Out of bounds index %" ISIZE_FMT " / %" ISIZE_FMT,
            ii,
            static_cast<isize>(N)
        );
        return this->data[ii];
    }

    template<class I>
    const T &
    operator[](I i) const
    {
        isize ii = static_cast<isize>(i);
        lulu_assertf(
            0 <= ii && ii < N,
            "Out of bounds index %" ISIZE_FMT " / %" ISIZE_FMT,
            ii,
            static_cast<isize>(N)
        );
        return this->data[ii];
    }
};

// `array[:]` in Odin.
template<class T, auto N>
constexpr Slice<T>
slice(Array<T, N> &a)
{
    Slice<T> s{a.data, N};
    return s;
}

// `array[start:stop]` in Odin.
template<class T, auto N>
inline Slice<T>
slice(Array<T, N> &a, isize start, isize stop)
{
    // `s := a[:]`
    Slice<T> s = slice(a);

    // `return s[start:stop]`
    return slice(s, start, stop);
}

// `array[start:]` in Odin.
template<class T, auto N>
inline Slice<T>
slice_from(Array<T, N> &a, isize start)
{
    return slice(a, start, len(a));
}

// `array[:stop]` in Odin.
template<class T, auto N>
inline Slice<T>
slice_until(Array<T, N> &a, isize stop)
{
    return slice(a, 0, stop);
}

template<class T, auto N>
constexpr isize
len(const Array<T, N> &a)
{
    unused(a);
    return N;
}

// Mutable access to underlying array data.
template<class T, auto N>
constexpr T *
raw_data(Array<T, N> &a)
{
    return a.data;
}

// Read-only access to underlying array data.
template<class T, auto N>
constexpr const T *
raw_data(const Array<T, N> &a)
{
    return a.data;
}

//=== }}} ======================================================================
