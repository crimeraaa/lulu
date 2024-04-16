#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"
#include "limits.h"

#define grow_capacity(N)    ((N) < 8 ? 8 : (N) * 2)

typedef void *(*AllocFn)(void *ptr, size_t oldsz, size_t newsz, void *context);
typedef void  (*FreeFn)(void *ptr, size_t usedsz, void *context);

typedef struct {
    AllocFn allocfn; // Allocate/reallocate a block of memory.
    FreeFn freefn;   // Free a block of memory.
    void *context;   // How this is interpreted is up to your functions.
} Allocator;

void init_allocator(Allocator *self, AllocFn allocfn, FreeFn freefn, void *context);
void free_objects(VM *vm);

#define new_array(T, N, allocator)                                             \
    (allocator)->allocfn(NULL,                                                 \
                         0,                                                    \
                         array_size(T, N),                                     \
                         (allocator)->context)

#define resize_array(T, ptr, oldcap, newcap, allocator)                        \
    (allocator)->allocfn((ptr),                                                \
                        array_size(T, oldcap),                                 \
                        array_size(T, newcap),                                 \
                        (allocator)->context)

#define free_array(T, ptr, len, allocator)                                     \
    (allocator)->freefn((ptr),                                                 \
                        array_size(T, len),                                    \
                        (allocator)->context)

// Needed by `memory.c:free_object()` and `object.c:copy_string()`.
#define free_tstring(ptr, len, allocator)                                      \
    (allocator)->freefn((ptr),                                                 \
                        tstring_size(len),                                     \
                        (allocator)->context)

#endif /* LULU_MEMORY_H */
