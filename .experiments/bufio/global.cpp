#include "global.hpp"

static void *stdc_allocate(void *ptr, size_t oldsz, size_t newsz, void *ctx)
{
    unused2(oldsz, ctx);
    if (newsz == 0) {
        free(ptr);
        return nullptr;
    }
    return std::realloc(ptr, newsz);
}

void init_global(Global *g)
{
    init_allocator(&g->allocator, &stdc_allocate, NULL);
    g->objects = NULL;
}
