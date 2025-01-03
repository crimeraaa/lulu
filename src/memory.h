#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"

#include <assert.h>

static inline isize
mem_grow_capacity(isize cap)
{
    isize tmp = 8;
    while (tmp <= cap) {
        tmp *= 2;
    }
    return tmp;
}

void *
mem_alloc(lulu_VM *vm, isize new_size);

void *
mem_resize(lulu_VM *vm, void *old_ptr, isize old_size, isize new_size);

void
mem_free(lulu_VM *vm, void *old_ptr, isize old_size);

#define array_new(Type, vm, count)                                             \
    cast(Type *)mem_alloc(vm, size_of(Type) * (count))

#define array_resize(Type, vm, old_ptr, old_count, new_count)                  \
    cast(Type *)mem_resize(vm, old_ptr,                                        \
        size_of((old_ptr)[0]) * (old_count),                                   \
        size_of((old_ptr)[0]) * (new_count))

#define array_free(Type, vm, ptr, count)                                       \
    mem_free(vm, ptr, size_of((ptr)[0]) * (count));                            \

#define ptr_new(Type, vm)        array_new(Type, vm, 1)
#define ptr_free(Type, vm, ptr)  array_free(Type, vm, ptr, 1)

#endif // LULU_MEMORY_H
