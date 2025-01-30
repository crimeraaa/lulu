#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"

#include <assert.h>

static inline int
mem_grow_capacity(isize cap)
{
    int tmp = 8;
    while (tmp <= cap) {
        tmp *= 2;
    }
    return tmp;
}

void *
mem_alloc(lulu_VM *vm, isize new_size, cstring file, int line);

void *
mem_resize(lulu_VM *vm, void *old_ptr, isize old_size, isize new_size, cstring file, int line);

void
mem_free(lulu_VM *vm, void *old_ptr, isize old_size, cstring file, int line);

#define mem_alloc(vm, new_size) \
    mem_alloc(vm, new_size, __FILE__, __LINE__)

#define mem_resize(vm, old_ptr, old_size, new_size) \
    mem_resize(vm, old_ptr, old_size, new_size, __FILE__, __LINE__)

#define mem_free(vm, old_ptr, old_size) \
    mem_free(vm, old_ptr, old_size, __FILE__, __LINE__)

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
