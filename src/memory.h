#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"

#include <assert.h>

static inline isize
mem_grow_capacity(isize cap)
{
    return (cap < 8) ? 8 : (cap * 2);
}

void *
mem_alloc(lulu_VM *vm, isize new_size);

void *
mem_resize(lulu_VM *vm, void *old_ptr, isize old_size, isize new_size);

void
mem_free(lulu_VM *vm, void *old_ptr, isize old_size);

#define rawarray_new(Type, vm, count)                                          \
    cast(Type *)mem_alloc(                                                     \
        vm,                                                                    \
        size_of(Type) * (count))

#define rawarray_resize(Type, vm, old_ptr, old_count, new_count)               \
    cast(Type *)mem_resize(                                                    \
        vm,                                                                    \
        old_ptr,                                                               \
        size_of((old_ptr)[0]) * (old_count),                                   \
        size_of((old_ptr)[0]) * (new_count))

// The sizeof check isn't foolproof but it should help somewhat.
#define rawarray_free(Type, vm, ptr, count)                                    \
do {                                                                           \
    static_assert(size_of(Type) == size_of((ptr)[0]), "Invalid type!");        \
    mem_free(vm, ptr, size_of((ptr)[0]) * (count));                            \
} while (0)

#define rawptr_new(Type, vm)        rawarray_new(Type, vm, 1)
#define rawptr_free(Type, vm, ptr)  rawarray_free(Type, vm, ptr, 1)

#endif // LULU_MEMORY_H
