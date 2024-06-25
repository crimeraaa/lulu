#pragma once

#include "conf.hpp"

struct Allocator {
    using Fn = void *(*)(void *ptr, size_t oldsz, size_t newsz, void *ctx);
    Fn    allocate;
    void *context;
};

void  init_allocator(Allocator *a, Allocator::Fn fn, void *ctx);
void *call_allocator(Global *g, void *p, size_t oldsz, size_t newsz);
#define free_pointer(g, p, sz)  call_allocator(g, p, sz, 0)

template<class T>
inline T *new_pointer(Global *g, size_t sz)
{
    return cast<T*>(call_allocator(g, nullptr, 0, sz));
}

template<class T>
inline T *resize_pointer(Global *g, T *p, size_t oldsz, size_t newsz)
{
    return cast<T*>(call_allocator(g, p, oldsz, newsz));
}
