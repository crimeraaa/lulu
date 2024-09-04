#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"

#define GROW_CAPACITY(cap)  ((cap) < 8 ? 8 : (cap) * 2)

typedef enum {
    LULU_ALLOCATOR_MODE_ALLOC,
    LULU_ALLOCATOR_MODE_RESIZE,
    LULU_ALLOCATOR_MODE_FREE,
} lulu_Allocator_Mode;

typedef void *(*lulu_Allocator_Proc)(
    void *              allocator_data,
    lulu_Allocator_Mode mode,
    isize               new_size,
    isize               align,
    void *              old_ptr,
    isize               old_size);

typedef struct {
    lulu_Allocator_Proc procedure;
    void               *data;
} lulu_Allocator;

#ifndef LULU_NOSTDLIB

/**
 * @brief
 *      A simple allocator that wraps the C standard `malloc` family.
 *      
 * @warning 2024-09-04
 *      This will call `abort()` on allocation failure!
 */
extern const lulu_Allocator lulu_heap_allocator;

#endif // LULU_NOSTDLIB

void *lulu_Allocator_alloc(const lulu_Allocator *self, isize new_size, isize align);
void *lulu_Allocator_resize(const lulu_Allocator *self, void *old_ptr, isize old_size, isize new_size, isize align);
void lulu_Allocator_free(const lulu_Allocator *self, void *old_ptr, isize old_size);

#define rawptr_new(Type, allocator)                                            \
    cast(Type *)lulu_Allocator_alloc(                                          \
        allocator,                                                             \
        size_of(Type),                                                         \
        align_of(Type))

#define rawptr_free(Type, ptr, allocator)                                      \
    lulu_Allocator_free(                                                       \
        allocator,                                                             \
        ptr,                                                                   \
        size_of(Type))

#define rawarray_new(Type, count, allocator)                                   \
    cast(Type *)lulu_Allocator_alloc(                                          \
        allocator,                                                             \
        size_of(Type) * (count),                                               \
        align_of(Type))

#define rawarray_resize(Type, old_ptr, old_count, new_count, allocator)        \
    cast(Type *)lulu_Allocator_resize(                                         \
        allocator,                                                             \
        old_ptr,                                                               \
        size_of(Type) * (old_count),                                           \
        size_of(Type) * (new_count),                                           \
        align_of(Type))

#define rawarray_free(Type, old_ptr, old_count, allocator)                     \
    lulu_Allocator_free(allocator,                                             \
        old_ptr,                                                               \
        size_of(Type) * (old_count))

#endif // LULU_MEMORY_H
