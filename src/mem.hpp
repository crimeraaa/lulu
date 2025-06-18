#pragma once

#include "lulu.h"
#include "private.hpp"

/**
 * @brief
 *  -   Rounds `n` to the next multiple of 2 if it is not one already.
 */
size_t
mem_next_size(size_t n);

void *
mem_rawrealloc(lulu_VM &vm, void *ptr, size_t old_size, size_t new_size);

template<class T>
inline T *
mem_new(lulu_VM &vm, size_t extra = 0)
{
    return cast(T *, mem_rawrealloc(vm, nullptr, 0, sizeof(T) + extra));
}

template<class T>
inline T *
mem_resize(lulu_VM &vm, T *ptr, size_t prev, size_t next)
{
    return cast(T *, mem_rawrealloc(vm, ptr, sizeof(T) * prev, sizeof(T) * next));
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
