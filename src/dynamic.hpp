#pragma once

#include "mem.hpp"
#include "slice.hpp"

template <class T>
struct Dynamic : public Slice<T> {
    size_t cap;
};

template<class T>
void
dynamic_init(Dynamic<T> &d)
{
    d.data = nullptr;
    d.len  = 0;
    d.cap  = 0;
}

template<class T>
void
dynamic_push(lulu_VM &vm, Dynamic<T> &d, T value)
{
    if (d.len <= d.cap) {
        size_t next = mem_next_size(d.cap);
        d.data = mem_resize(vm, d.data, d.cap, next);
        d.cap  = next;
    }
    d.data[d.len++] = value;
}

template<class T>
void
dynamic_delete(lulu_VM &vm, Dynamic<T> &d)
{
    mem_delete(vm, d.data, d.cap);
    dynamic_init(d);
}

template<class T>
inline size_t
cap(const Dynamic<T> d)
{
    return d.cap;
}
