#pragma once

#include "private.hpp"

template<class T, isize N>
struct Array {
    T data[N];

    template<class I>
    T &
    operator[](I i)
    {
        isize ii = cast_isize(i);
        lulu_assertf(0 <= ii && ii < N,
            "Out of bounds index %" ISIZE_FMTSPEC " / %" ISIZE_FMTSPEC,
            ii, N);
        return this->data[ii];
    }

    template<class I>
    const T &
    operator[](I i) const
    {
        isize ii = cast_isize(i);
        lulu_assertf(0 <= ii && ii < N,
            "Out of bounds index %" ISIZE_FMTSPEC " / %" ISIZE_FMTSPEC,
            ii, N);
        return this->data[ii];
    }
};

template<class T, auto N>
LULU_FUNC constexpr isize
len(const Array<T, N> &a)
{
    unused(a);
    return N;
}

template<class T, auto N>
LULU_FUNC constexpr T *
raw_data(Array<T, N> &a)
{
    return a.data;
}
