#pragma once

#include <string.h> // memcmp, memmove

#include "array.hpp"

template<class T>
struct Slice {
    T    *data;
    isize len;

    // Bounds-checked, mutable element access.
    template<class N>
    T &
    operator[](N i)
    {
        isize ii = cast_isize(i);
        lulu_assertf(ii < this->len,
            "Out of bounds index %" ISIZE_FMTSPEC " / %" ISIZE_FMTSPEC,
            ii, this->len);
        return this->data[ii];
    }

    // Bounds-checked, non-mutable element access.
    template<class N>
    const T &
    operator[](N i) const
    {
        isize ii = cast_isize(i);
        lulu_assertf(ii < this->len,
            "Out of bounds index %" ISIZE_FMTSPEC " / %" ISIZE_FMTSPEC,
            ii, this->len);
        return this->data[ii];
    }
};

template<class T>
LULU_FUNC inline Slice<T>
slice_pointer(T *start, T *stop)
{
    Slice<T> s{start, cast_isize(stop - start)};
    // Problem: comparison of 2 unrelated pointers is not standard!
    lulu_assertf(start <= stop,
        "start=%p > stop=%p",
        cast(void *)start, cast(void *)stop);
    return s;
}

template<class T>
LULU_FUNC inline Slice<T>
slice_slice(Slice<T> &s)
{
    return {s.data, s.len};
}

template<class T>
LULU_FUNC inline Slice<T>
slice_slice(Slice<T> &s, isize start, isize stop)
{
    Slice<T> s2{&s.data[start], stop - start};
    lulu_assertf(0 <= len(s2) && len(s2) <= len(s),
        "invalid result length: len(s2)=%" ISIZE_FMTSPEC " > len(s)=%" ISIZE_FMTSPEC,
        len(s2), len(s));

    lulu_assertf(0 <= start && start <= len(s),
        "invalid start index: start=%" ISIZE_FMTSPEC " > %" ISIZE_FMTSPEC,
        start, len(s));

    lulu_assertf(0 <= stop && stop <= len(s),
        "invalid stop index: stop=%" ISIZE_FMTSPEC " > %" ISIZE_FMTSPEC,
        stop, len(s));

    lulu_assertf(start <= stop,
        "invalid start-stop pair: start=%" ISIZE_FMTSPEC " > stop=%" ISIZE_FMTSPEC,
        start, stop);
    return s2;
}

template<class T, auto N>
LULU_FUNC inline Slice<T>
slice_array(Array<T, N> &a, isize start, isize stop)
{
    Slice<T> s{&a.data[start], stop - start};
    lulu_assertf(0 <= len(s) && len(s) <= N,
        "invalid result length: len(s)=%" ISIZE_FMTSPEC " > N=%" ISIZE_FMTSPEC,
        len(s), N);

    lulu_assertf(0 <= start && start <= N,
        "invalid start index: start=%" ISIZE_FMTSPEC " > N=%" ISIZE_FMTSPEC,
        start, N);

    lulu_assertf(0 <= stop && stop <= N,
        "invalid stop index: stop=%" ISIZE_FMTSPEC " > N=%" ISIZE_FMTSPEC,
        stop, N);

    lulu_assertf(start <= stop,
        "invalid start-stop pair: start=%" ISIZE_FMTSPEC " > stop=%" ISIZE_FMTSPEC,
        start, stop);
    return s;
}

template<class T>
LULU_FUNC inline bool
slice_eq(Slice<T> a, Slice<T> b)
{
    if (len(a) != len(b)) {
        return false;
    }
    usize size = sizeof(T) * cast_usize(len(a));
    return memcmp(raw_data(a), raw_data(b), size) == 0;
}

template<class T>
LULU_FUNC constexpr isize
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
    isize n = (dst.len > src.len) ? src.len : dst.len;
    memmove(dst.data, src.data, sizeof(T) * cast_usize(n));
}

// Because C++ templates aren't convoluted enough
template<class T>
LULU_FUNC inline void
copy(Slice<T> dst, Slice<const T> src)
{
    // Clamp size to read and copy
    isize n = (dst.len > src.len) ? src.len : dst.len;
    memmove(dst.data, src.data, sizeof(T) * cast_usize(n));
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
