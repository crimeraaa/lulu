#include "mem.hpp"
#include "global.hpp"

void init_allocator(Allocator *a, Allocator::Fn fn, void *ctx)
{
    a->allocate = fn;
    a->context  = ctx;
}

void *call_allocator(Global *g, void *p, size_t oldsz, size_t newsz)
{
    p = g->allocator.allocate(p, oldsz, newsz, g->allocator.context);
    if (p == nullptr && newsz > 0)
        throw std::bad_alloc();
    return p;
}
