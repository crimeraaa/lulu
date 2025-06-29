#pragma once

#include "private.hpp"
#include "slice.hpp"

/**
 * @brief
 *  -   Rounds `n` to the next power of 2 if it is not one already.
 */
size_t
mem_next_size(size_t n);

void *
mem_rawrealloc(lulu_VM &vm, void *ptr, size_t old_size, size_t new_size);

template<class T>
inline T *
mem_new(lulu_VM &vm, size_t extra = 0)
{
    return cast(T *)mem_rawrealloc(vm, nullptr, 0, sizeof(T) + extra);
}

template<class T>
inline void
mem_free(lulu_VM &vm, T *ptr, size_t extra = 0)
{
    mem_rawrealloc(vm, ptr, sizeof(T) + extra, 0);
}

template<class T>
inline T *
mem_resize(lulu_VM &vm, T *ptr, size_t prev, size_t next)
{
    return cast(T *)mem_rawrealloc(vm, ptr, sizeof(T) * prev, sizeof(T) * next);
}

template<class T>
inline T *
mem_make(lulu_VM &vm, size_t count)
{
    return mem_resize<T>(vm, nullptr, 0, count);
}

template<class T>
inline void
mem_delete(lulu_VM &vm, T *ptr, size_t n)
{
    mem_resize(vm, ptr, n, 0);
}

template<class T>
inline Slice<T>
slice_make(lulu_VM &vm, typename Slice<T>::size_type n)
{
    return Slice(mem_make<T>(vm, n), n);
}

template<class T>
inline void
slice_delete(lulu_VM &vm, Slice<T> s)
{
    mem_delete(vm, raw_data(s), len(s));
}

template<class T>
inline typename Slice<T>::size_type
ptr_index(Slice<T> s, T *p)
{
    return cast_size(p - raw_data(s));
}

template<class T, size_t N>
inline typename Slice<T>::size_type
ptr_index(Array<T, N> &a, T *p)
{
    return cast_size(p - raw_data(a));
}

template<class T>
inline size_t
ptr_index(T *data, T *ptr)
{
    return cast_size(ptr - data);
}
