#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"
#include "limits.h"

#define grow_capacity(N)    ((N) < 8 ? 8 : (N) * 2)

typedef void *(*ReallocFn)(void *ptr, size_t oldsz, size_t newsz, void *context);

// A general purpose allocation wrapper that carries some context around.
// See: https://nullprogram.com/blog/2023/12/17/
typedef struct {
    ReallocFn reallocfn; // To free `ptr`, pass `newsz` of `0`.
    void     *context;   // How this is interpreted is up to your function.
} Alloc;

// Once set, please do not reinitialize your allocator else it may break.
void init_alloc(Alloc *self, ReallocFn reallocfn, void *context);
void free_objects(VM *vm);

#define new_array(T, N, alloc)                                                 \
    (alloc)->reallocfn((NULL),                                                 \
                        0,                                                     \
                        array_size(T, N),                                      \
                        (alloc)->context)

#define resize_array(T, ptr, oldcap, newcap, alloc)                            \
    (alloc)->reallocfn((ptr),                                                  \
                        array_size(T, oldcap),                                 \
                        array_size(T, newcap),                                 \
                        (alloc)->context)

#define free_array(T, ptr, len, alloc)                                         \
    (alloc)->reallocfn((ptr),                                                  \
                        array_size(T, len),                                    \
                        0,                                                     \
                        (alloc)->context)

#define free_pointer(T, ptr, alloc) \
    free_array(T, ptr, 1, alloc)

// Needed by `memory.c:free_object()` and `object.c:copy_string()`.
// Note we add 1 to `oldsz` because we previously allocated 1 extra by for nul.
#define free_tstring(ptr, len, alloc)                                          \
    (alloc)->reallocfn((ptr),                                                  \
                        tstring_size(len) + 1,                                 \
                        0,                                                     \
                        (alloc)->context)

#endif /* LULU_MEMORY_H */
