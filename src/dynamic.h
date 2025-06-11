#pragma once

#include "mem.h"
#include "slice.h"

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
dynamic_resize(lulu_VM &vm, Dynamic<T> &d, size_t cap)
{
    d.data = mem_resize(vm, d.data, d.cap, cap);
    d.cap  = cap;
}

template<class T>
void
dynamic_push(lulu_VM &vm, Dynamic<T> &d, T value)
{
    if (d.len <= d.cap) {
        size_t next = mem_next_size(d.cap);
        dynamic_resize(vm, d, next);
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
cap(const Dynamic<T> &self)
{
    return self.cap;
}
