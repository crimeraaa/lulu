#pragma once

#include <string.h> // memmove

template <class T>
struct Slice {
    T     *data;
    size_t len;
    
    //=== READ-WRITE OPERATIONS ============================================ {{{
    
    // Bounds-checked, mutable element access.
    T &operator[](size_t index)
    {
        lulu_assert(0 <= index && index < this->len);
        return this->data[index];
    }

    // Mutable forward iterator start.
    T *begin() noexcept
    {
        return this->data;
    }

    // Mutabel forward iterator stop.
    T *end() noexcept
    {
        return this->data + this->len;
    }

    //=== }}} ==================================================================

    //=== READ-ONLY OPERATIONS ============================================= {{{

    // Bounds-checked, non-mutable element access.
    const T &operator[](size_t index) const
    {
        lulu_assert(0 <= index && index < this->len);
        return this->data[index];
    }

    // Non-mutable forward iterator initial value.
    const T *begin() const noexcept
    {
        return this->data;
    }

    // Non-mutable forward iterator final value.
    const T *end() const noexcept
    {
        return this->data + this->len;
    }

    //=== }}} ==================================================================
};

template<class T>
inline size_t
len(const Slice<T> &s)
{
    return s.len;
}

template<class T>
inline T *
raw_data(Slice <T> &s)
{
    return s.data;
}

template<class T>
inline const T *
raw_data(const Slice<T> &s)
{
    return s.data;
}

template<class T>
inline void
copy(Slice<T> &dst, const Slice<T> &src)
{
    // Clamp size to read and copy
    const size_t n = (dst.len > src.len) ? src.len : dst.len;
    memmove(dst.data, src.data, sizeof(T) * n);
}

// Because C++ templates aren't convoluted enough
template<class T>
inline void
copy(Slice <T> &dst, const Slice<const T> &src)
{
    // Clamp size to read and copy
    const size_t n = (dst.len > src.len) ? src.len : dst.len;
    memmove(dst.data, src.data, sizeof(T) * n);
}

using String = Slice<const char>;
