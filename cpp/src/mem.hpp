#pragma once

#include "private.hpp"
#include "slice.hpp"

void *
mem_rawrealloc(lulu_VM *vm, void *ptr, usize old_size, usize new_size);


/**
 * @param n
 *      Some nonzero, positive integer. Recall that `log(0)` for any base
 *      is undefined.
 *
 * @return
 *      Exponent of the next power of 2 after `n`, if it is not one already.
 */
int
mem_ceil_log2(usize n);


/**
 * @brief
 *      Rounds `n` to the next power of 2 if it is not one already.
 */
inline isize
mem_next_pow2(isize n)
{
    int e = mem_ceil_log2(cast_usize(n));
    return 1_i << e;
}


/**
 * @brief
 *      Rounds `n` to the next fibonacci term, if it is not one already.
 */
inline isize
mem_next_fib(isize n)
{
    // Don't start with 1 because ((1*3) >> 1) == 1) meaning an infinite loop.
    isize next = 2;
    while (next < n) {
        // (x*3) >> 1 <=> (x*3) / 2 <=> x*1.5
        // 1.5 is the closest approximation of the Fibonacci factor of 1.618...
        next = (next * 3) >> 1;
    }
    return next;
}

// `extra` may be negative to allow us to work with 0-length flexible arrays.
template<class T>
inline T *
mem_new(lulu_VM *vm, isize extra = 0)
{
    // Cast must occur after arithmetic in case `extra < 0`.
    usize size = cast_usize(size_of(T) + extra);
    return reinterpret_cast<T *>(mem_rawrealloc(vm, nullptr, 0, size));
}

template<class T>
inline void
mem_free(lulu_VM *vm, T *ptr, isize extra = 0)
{
    usize size = cast_usize(size_of(T) + extra);
    mem_rawrealloc(vm, ptr, size, 0);
}

template<class T>
inline T *
mem_resize(lulu_VM *vm, T *ptr, isize prev, isize next)
{
    usize prev_size = sizeof(T) * cast_usize(prev);
    usize next_size = sizeof(T) * cast_usize(next);
    return reinterpret_cast<T *>(mem_rawrealloc(vm, ptr, prev_size, next_size));
}

template<class T>
inline T *
mem_make(lulu_VM *vm, isize count)
{
    return mem_resize<T>(vm, nullptr, 0, count);
}

template<class T>
inline void
mem_delete(lulu_VM *vm, T *ptr, isize n)
{
    mem_resize(vm, ptr, n, 0);
}

template<class T>
inline Slice<T>
slice_make(lulu_VM *vm, isize n)
{
    Slice<T> s{mem_make<T>(vm, n), n};
    return s;
}

template<class T>
inline void
slice_resize(lulu_VM *vm, Slice<T> *s, isize n)
{
    Slice<T> next{mem_resize<T>(vm, raw_data(*s), len(*s), n), n};
    *s = next;
}

template<class T>
inline void
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
inline int
ptr_index(Slice<T> s, const T *p)
{
    return cast_int(p - raw_data(s));
}

template<class T, auto N>
inline int
ptr_index(const Array<T, N> &a, const T *p)
{
    return cast_int(p - raw_data(a));
}


/**
 * @param [out] i
 *      Holds the index of `p` in `s.data` if it is indeed contained,
 *      otherwise is not assigned and may have garbage data.
 */
template<class T>
inline bool
ptr_index_safe(Slice<T> s, const T *p, int *i)
{
    auto addr_start = cast(uintptr_t)begin(s);
    auto addr_stop  = cast(uintptr_t)end(s);
    auto addr_test  = cast(uintptr_t)p;

    // Integer representation of `p` is within the array?
    if (addr_start <= addr_test && addr_test < addr_stop) {
        *i = ptr_index(s, p);
        return true;
    }
    return false;
}
