#pragma once

#include "conf.hpp"

struct Allocator {
    using Fn = void *(*)(void *ptr, size_t oldsz, size_t newsz, void *ctx);
    Fn    allocate;
    void *context;
};

void  init_allocator(Allocator *a, Allocator::Fn fn, void *ctx);
void *call_allocator(Global *g, void *p, size_t oldsz, size_t newsz);

#define grow_capacity(N)    ((N) > 8 ? (N) * 2 : 8)

template<class T>
inline T *new_pointer(Global *g, size_t sz = sizeof(T))
{
    return cast<T*>(call_allocator(g, nullptr, 0, sz));
}

template<class T>
inline T *resize_pointer(Global *g, T *p, size_t oldsz, size_t newsz)
{
    return cast<T*>(call_allocator(g, p, oldsz, newsz));
}

template<class T>
inline void free_pointer(Global *g, T *p, size_t sz = sizeof(T))
{
    call_allocator(g, p, sz, 0);
}

template<class T>
inline T *new_array(Global *g, size_t n)
{
    return new_pointer<T>(g, sizeof(T) * n);    
}

template<class T>
inline T *resize_array(Global *g, T *p, size_t oldn, size_t newn)
{
    return resize_pointer(g, p, sizeof(T) * oldn, sizeof(T) * newn);
}

template<class T>
inline void free_array(Global *g, T *p, size_t n)
{
    free_pointer(g, p, sizeof(T) * n);
}
