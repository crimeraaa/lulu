#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"
#include "limits.h"
#include "object.h"

#define grow_capacity(N)    ((N) < 8 ? 8 : (N) * 2)

typedef void *(*AllocFn)(void *ptr, size_t oldsz, size_t newsz, void *ctx);

// A general purpose allocation wrapper that carries some context around.
// See: https://nullprogram.com/blog/2023/12/17/
struct lulu_Alloc {
    AllocFn allocfn; // To free `ptr`, pass `newsz` of `0`.
    void   *context; // How this is interpreted is up to your function.
};

// Once set, please do not reinitialize your allocator else it may break.
void init_alloc(Alloc *al, AllocFn fn, void *ctx);

// Assumes casting `al->context` to `VM*` is a safe operation.
Object *new_object(size_t size, VType tag, Alloc *al);
void free_objects(lulu_VM *vm);

// `prepend_object()` and `remove_object()` both return `o`.
Object *prepend_object(Object **head, Object *obj);
Object *remove_object(Object **head, Object *obj);

#define new_pointer(size, alloc) \
    (alloc)->allocfn((NULL), 0, (size), (alloc)->context)

#define resize_pointer(ptr, oldsz, newsz, alloc) \
    (alloc)->allocfn((ptr), (oldsz), (newsz), (alloc)->context)

#define free_pointer(ptr, size, alloc) \
    (alloc)->allocfn((ptr), (size), 0, (alloc)->context)

#define new_array(T, N, alloc) \
    new_pointer(array_size(T, N), alloc)

#define resize_array(T, P, oldcap, newcap, alloc) \
    resize_pointer(P, array_size(T, oldcap), array_size(T, newcap), alloc)

#define free_array(T, P, len, alloc) \
    free_pointer(P, array_size(T, len), alloc)

#define new_parray(P, N, alloc) \
    new_pointer(parray_size(P, N), alloc)

#define resize_parray(P, oldcap, newcap, alloc) \
    resize_pointer(P, parray_size(P, oldcap), parray_size(P, newcap), alloc)

#define free_parray(P, len, alloc) \
    free_pointer(P, parray_size(P, len), alloc)

#endif /* LULU_MEMORY_H */
