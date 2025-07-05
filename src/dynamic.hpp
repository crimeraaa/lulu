#pragma once

#include "slice.hpp"
#include "mem.hpp"

template<class T>
struct Dynamic : public Slice<T> {
    typename Slice<T>::size_type cap;
};

template<class T>
LULU_FUNC inline void
dynamic_init(Dynamic<T> *d)
{
    d->data = nullptr;
    d->len  = 0;
    d->cap  = 0;
}

/**
 * @brief
 *  - Allocates memory to hold `new_cap` elements and sets `d.cap`.
 *  - `d.len` is left untouched thus you still cannot index it.
 */
template<class T>
LULU_FUNC inline void
dynamic_reserve(lulu_VM *vm, Dynamic<T> *d, typename Dynamic<T>::size_type new_cap)
{
    d->data = mem_resize(vm, d->data, d->cap, new_cap);
    d->cap  = new_cap;
}


/**
 * @brief 2025-06-12
 *  -   Allocates memory to hold at least `new_len` elements.
 *  -   If shrinking, no new memory is allocated but the valid indexable range
 *      is reduced.
 *  -   We clamp the size to reduce the number of consecutive reallocations.
 *  -   Unlike `dynamic_reserve()` this also sets `d.len` so you can safely
 *      index this range.
 */
template<class T>
LULU_FUNC inline void
dynamic_resize(lulu_VM *vm, Dynamic<T> *d, typename Slice<T>::size_type new_len)
{
    // Can't accomodate the new data?
    if (new_len > d->cap) {
        // Use 8 as the first size for slight optimization.
        size_t n = 8;

        // Get closest golden ratio term
        while (n < new_len) {
            // 1.5 is closest to golden ratio of 1.618...
            // (n*3) >> 1 == (n*3) / 2 == n*1.5
            n = (n*3) >> 1;
        }
        dynamic_reserve(vm, d, n);
    }
    d->len = new_len;
}

template<class T>
LULU_FUNC inline void
dynamic_push(lulu_VM *vm, Dynamic<T> *d, T value)
{
    dynamic_resize(vm, d, d->len + 1);
    (*d)[d->len - 1] = value;
}

template<class T>
LULU_FUNC inline T
dynamic_pop(Dynamic<T> *d)
{
    T value = (*d)[d->len - 1];
    d->len -= 1;
    return value;
}

template<class T>
LULU_FUNC inline void
dynamic_delete(lulu_VM *vm, Dynamic<T> *d)
{
    mem_delete(vm, d->data, d->cap);
}

template<class T>
LULU_FUNC inline void
dynamic_reset(Dynamic<T> *d)
{
    d->len = 0;
}

template<class T>
LULU_FUNC inline typename Dynamic<T>::size_type
cap(const Dynamic<T> &d)
{
    return d.cap;
}
