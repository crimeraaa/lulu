#pragma once

#include "private.hpp"

template<class T, size_t N>
struct Array {
    using size_type = size_t;

    T data[N];

    template<class I>
    T &
    operator[](I i)
    {
        size_type ii = cast(size_type)i;
        lulu_assertf(ii < N, "Out of bounds index %zu / %zu", ii, N);
        return this->data[ii];
    }

    template<class I>
    const T &
    operator[](I i) const
    {
        size_type ii = cast(size_type)i;
        lulu_assertf(ii < N, "Out of bounds index %zu / %zu", ii, N);
        return this->data[ii];
    }
};

template<class T, size_t N>
LULU_FUNC inline typename Array<T, N>::size_type
len(const Array<T, N> &a)
{
    unused(a);
    return N;
}

template<class T, size_t N>
LULU_FUNC inline T *
raw_data(Array<T, N> &a)
{
    return a.data;
}
