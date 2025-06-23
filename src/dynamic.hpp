#pragma once

#include "mem.hpp"
#include "slice.hpp"

template<class T>
struct Dynamic : public Slice<T> {
    size_t cap;
};

template<class T>
inline void
dynamic_init(Dynamic<T> &d)
{
    d.data = nullptr;
    d.len  = 0;
    d.cap  = 0;
}

/**
 * @brief
 *  - Allocates memory to hold `new_cap` elements and sets `d.cap`.
 *  - `d.len` is left untouched thus you still cannot index it.
 */
template<class T>
inline void
dynamic_reserve(lulu_VM &vm, Dynamic<T> &d, size_t new_cap)
{
    d.data = mem_resize(vm, d.data, d.cap, new_cap);
    d.cap  = new_cap;
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
inline void
dynamic_resize(lulu_VM &vm, Dynamic<T> &d, size_t new_len)
{
    // Can't accomodate the new data?
    if (new_len > d.cap) {
        // 1.5 is closest to golden ratio of 1.618
        // (n*3) >> 1 == (n*3) / 2 == n*1.5
        dynamic_reserve(vm, d, new_len*3 >> 1);
    }
    d.len = new_len;
}

template<class T>
inline void
dynamic_push(lulu_VM &vm, Dynamic<T> &d, T value)
{
    dynamic_resize(vm, d, d.len + 1);
    d[d.len - 1] = value;
}

template<class T>
inline T
dynamic_pop(Dynamic<T> &d)
{
    T value = d[d.len - 1];
    d.len -= 1;
    return value;
}

template<class T>
inline void
dynamic_delete(lulu_VM &vm, Dynamic<T> &d)
{
    mem_delete(vm, d.data, d.cap);
}

template<class T>
inline void
dynamic_reset(Dynamic<T> &d)
{
    d.len = 0;
}

template<class T>
inline size_t
cap(Dynamic<T> d)
{
    return d.cap;
}
