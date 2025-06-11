#pragma once

#include "mem.h"

template <class T>
struct Dynamic {
    T     *data;
    size_t len;
    size_t cap;


    T &operator[](size_t index)
    {
        if (0 <= index && index < this->len) {
            return this->data[index];
        }
        __builtin_trap();
    }

    T *begin()
    {
        return this->data;
    }

    T *end()
    {
        return this->data + this->len;
    }

    const T &operator[](size_t index) const
    {
        if (0 <= index && index < this->len) {
            return this->data[index];
        }
        __builtin_trap();
    }

    const T *begin() const noexcept
    {
        return this->data;
    }

    const T *end() const noexcept
    {
        return this->data + this->len;
    }
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
        size_t next = mem_next_pow2(d.cap);
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
