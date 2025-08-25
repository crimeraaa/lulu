#pragma once

#include "mem.hpp"
#include "slice.hpp"

template<class T>
struct Dynamic : public Slice<T> {
    isize cap = 0;
};

template<class T>
inline void
dynamic_init(Dynamic<T> *d)
{
    d->data = nullptr;
    d->len  = 0;
    d->cap  = 0;
}

/**
 * @brief
 *      Allocates memory to hold `new_cap` elements and sets `d.cap`.
 *      `d->len` is left untouched thus you still cannot index it.
 */
template<class T>
inline void
dynamic_reserve(lulu_VM *vm, Dynamic<T> *d, isize new_cap)
{
    d->data = mem_resize(vm, d->data, d->cap, new_cap);
    d->cap  = new_cap;
}


/**
 * @brief 2025-06-12
 *      Allocates memory to hold at least `new_len` elements. If shrinking,
 *      no new memory is allocated but the valid indexable range is reduced.
 *
 *      We clamp the size to reduce the number of consecutive reallocations.
 *      Unlike `reserve()` this also sets `d.len` so you can safely index
 *      this range.
 */
template<class T>
inline void
dynamic_resize(lulu_VM *vm, Dynamic<T> *d, isize new_len)
{
    // Can't accomodate the new data?
    if (new_len > d->cap) {
        // Use minimum cap of 8 for performance.
        isize new_cap = max(new_len, 8_i);
        dynamic_reserve(vm, d, mem_next_fib(new_cap));
    }
    d->len = new_len;
}

template<class T>
inline void
dynamic_push(lulu_VM *vm, Dynamic<T> *d, T value)
{
    dynamic_resize(vm, d, d->len + 1);
    (*d)[d->len - 1] = value;
}

template<class T>
inline void
dynamic_pop(Dynamic<T> *d)
{
    d->len -= 1;
}

template<class T>
inline void
dynamic_delete(lulu_VM *vm, Dynamic<T> *d)
{
    mem_delete(vm, d->data, d->cap);
}

template<class T>
inline void
dynamic_reset(Dynamic<T> *d)
{
    d->len = 0;
}

template<class T>
inline isize
cap(const Dynamic<T> &d)
{
    return d.cap;
}
