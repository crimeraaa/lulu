#pragma once

#include "private.hpp"
#include "slice.hpp"

LULU_FUNC void *
mem_rawrealloc(lulu_VM *vm, void *ptr, usize old_size, usize new_size);


/**
 * @brief
 *      Rounds `n` to the next power of 2 if it is not one already.
 */
LULU_FUNC inline isize
mem_next_pow2(isize n)
{
    isize next = 8;
    while (next <= n) {
        // x << 1 <=> x * 2 if x is a power of 2
        next <<= 1;
    }
    return next;
}


/**
 * @brief
 *      Rounds `n` to the next fibonacci term, if it is not one already.
 */
LULU_FUNC inline isize
mem_next_fib(isize n)
{
    // Use 8 at the first size for optimization.
    isize next = 8;
    while (next <= n) {
        // (x*3) >> 1 <=> (x*3) / 2 <=> x*1.5
        // 1.5 is the closest approximation of the Fibonacci factor of 1.618...
        next = (next * 3) >> 1;
    }
    return next;
}

// `extra` may be negative to allow us to work with 0-length flexible arrays.
template<class T>
LULU_FUNC inline T *
mem_new(lulu_VM *vm, isize extra = 0)
{
    // Cast must occur after arithmetic in case `extra < 0`.
    usize size = cast_usize(size_of(T) + extra);
    return reinterpret_cast<T *>(mem_rawrealloc(vm, nullptr, 0, size));
}

template<class T>
LULU_FUNC inline void
mem_free(lulu_VM *vm, T *ptr, isize extra = 0)
{
    usize size = cast_usize(size_of(T) + extra);
    mem_rawrealloc(vm, ptr, size, 0);
}

template<class T>
LULU_FUNC inline T *
mem_resize(lulu_VM *vm, T *ptr, isize prev, isize next)
{
    usize prev_size = sizeof(T) * cast_usize(prev);
    usize next_size = sizeof(T) * cast_usize(next);
    return reinterpret_cast<T *>(mem_rawrealloc(vm, ptr, prev_size, next_size));
}

template<class T>
LULU_FUNC inline T *
mem_make(lulu_VM *vm, isize count)
{
    return mem_resize<T>(vm, nullptr, 0, count);
}

template<class T>
LULU_FUNC inline void
mem_delete(lulu_VM *vm, T *ptr, isize n)
{
    mem_resize(vm, ptr, n, 0);
}

template<class T>
LULU_FUNC inline Slice<T>
slice_make(lulu_VM *vm, isize n)
{
    Slice<T> s{mem_make<T>(vm, n), n};
    return s;
}

template<class T>
LULU_FUNC inline void
slice_delete(lulu_VM *vm, Slice<T> s)
{
    mem_delete(vm, raw_data(s), len(s));
}


/**
 * @warning(2025-07-22)
 *
 *  1.) Assumes `p` indeed points to something within `raw_data(s)`. Otherwise
 *      it is undefined behavior to perform pointer arithmetic on 2 unrelated
 *      pointers.
 */
template<class T>
LULU_FUNC inline isize
ptr_index(const Slice<T> &s, const T *p)
{
    return cast_isize(p - raw_data(s));
}

template<class T, isize N>
LULU_FUNC inline isize
ptr_index(const Array<T, N> &a, const T *p)
{
    return cast_isize(p - raw_data(a));
}

template<class T>
LULU_FUNC inline bool
ptr_index_safe(const Slice<T> &s, const T *p, isize *out)
{
    auto addr_start = cast(uintptr_t)begin(s);
    auto addr_stop  = cast(uintptr_t)end(s);
    auto addr_test  = cast(uintptr_t)p;

    // Integer representation of `p` is within the array?
    if (addr_start <= addr_test && addr_test < addr_stop) {
        *out = ptr_index(s, p);
        return true;
    }
    return false;
}
