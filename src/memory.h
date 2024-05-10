#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"
#include "limits.h"

// Defined in `object.h`.
typedef struct Object Object;

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

// Prepend `node` to the linked list pointer pointed to by `head`. Returns `node`.
Object *prepend_object(Object **head, Object *node);

// Remove node from the linked list pointer pointed to by `head`. Returns `node`.
Object *remove_object(Object **head, Object *node);

#define new_pointer(size, alloc) \
    (alloc)->reallocfn((NULL), 0, (size), (alloc)->context)

#define resize_pointer(ptr, oldsz, newsz, alloc) \
    (alloc)->reallocfn((ptr), (oldsz), (newsz), (alloc)->context)

#define free_pointer(ptr, size, alloc) \
    (alloc)->reallocfn((ptr), (size), 0, (alloc)->context)

#define new_array(T, N, alloc) \
    new_pointer(array_size(T, N), alloc)

#define resize_array(T, ptr, oldcap, newcap, alloc) \
    resize_pointer(ptr, array_size(T, oldcap), array_size(T, newcap), alloc)

#define free_array(T, ptr, len, alloc) \
    free_pointer(ptr, array_size(T, len), alloc)

#endif /* LULU_MEMORY_H */
