#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"

#define GROW_CAPACITY(cap)  ((cap) < 8 ? 8 : (cap) * 2)

typedef union {
    void * p;
    double d;
    long   l;
} lulu_Allocator_Alignment;

#define LULU_ALLOCATOR_ALIGNMENT sizeof(lulu_Allocator_Alignment)

typedef enum {
    LULU_ALLOCATOR_MODE_ALLOC,
    LULU_ALLOCATOR_MODE_RESIZE,
    LULU_ALLOCATOR_MODE_FREE,
} lulu_Allocator_Mode;

typedef void *(*lulu_Allocator)(
    void *allocator_data,
    lulu_Allocator_Mode mode,
    isize new_size,
    isize align,
    void *old_ptr,
    isize old_size);

void *lulu_Allocator_alloc(lulu_VM *vm, isize new_size);
void *lulu_Allocator_resize(lulu_VM *vm, void *old_ptr, isize old_size, isize new_size);
void lulu_Allocator_free(lulu_VM *vm, void *old_ptr, isize old_size);

#define rawptr_new(Type, vm)                                                   \
    cast(Type *)lulu_Allocator_alloc(                                          \
        vm,                                                                    \
        size_of(Type))

#define rawptr_free(Type, vm, ptr)                                             \
    lulu_Allocator_free(                                                       \
        vm,                                                                    \
        ptr,                                                                   \
        size_of(Type))

#define rawarray_new(Type, vm, count)                                          \
    cast(Type *)lulu_Allocator_alloc(                                          \
        vm,                                                                    \
        size_of(Type) * (count))

#define rawarray_resize(Type, vm, old_ptr, old_count, new_count)               \
    cast(Type *)lulu_Allocator_resize(                                         \
        vm,                                                                    \
        old_ptr,                                                               \
        size_of((old_ptr)[0]) * (old_count),                                   \
        size_of((old_ptr)[0]) * (new_count))

/**
 * @note 2024-09-05
 *      We don't need to use `Type` for anything since we can infer the size from
 *      `ptr`. However for uniformity we keep it around.
 */
#define rawarray_free(Type, vm, ptr, count)                                    \
    lulu_Allocator_free(                                                       \
        vm,                                                                    \
        ptr,                                                                   \
        size_of((ptr)[0]) * (count))

#endif // LULU_MEMORY_H
