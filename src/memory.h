#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"
#include "limits.h"
#include "object.h"

#define luluMem_grow_capacity(N)    ((N) < 8 ? 8 : (N) * 2)

typedef void *(*lulu_AllocFn)(void *ptr, size_t oldsz, size_t newsz, void *ctx);

// A general purpose allocation wrapper that carries some context around.
// See: https://nullprogram.com/blog/2023/12/17/
typedef struct lulu_Allocator {
    lulu_AllocFn allocate; // To free `ptr`, pass `newsz` of `0`.
    void        *context;  // How this is interpreted is up to your function.
} Allocator;

void  luluMem_set_allocator(lulu_VM *vm, lulu_AllocFn fn, void *ctx);
void *luluMem_call_allocator(lulu_VM *vm, void *ptr, size_t oldsz, size_t newsz);

Object *luluObj_new(lulu_VM *vm, size_t size, VType tag);
Object *luluObj_prepend(lulu_VM *vm, Object *obj);
Object *luluObj_remove(lulu_VM *vm, Object *obj);
void    luluObj_free_all(lulu_VM *vm);

#define luluMem_new_pointer(vm, sz) \
    luluMem_call_allocator(vm, NULL, 0, sz)

#define luluMem_resize_pointer(vm, ptr, oldsz, newsz) \
    luluMem_call_allocator(vm, ptr, oldsz, newsz)

#define luluMem_free_pointer(vm, ptr, sz) \
    luluMem_call_allocator(vm, ptr, sz, 0)

#define luluMem_new_parray(vm, ptr, len) \
    luluMem_new_pointer(vm, parray_size(ptr, len))

#define luluMem_resize_parray(vm, ptr, oldcap, newcap) \
    luluMem_resize_pointer(vm, ptr, parray_size(ptr, oldcap), parray_size(ptr, newcap))

#define luluMem_free_parray(vm, ptr, len) \
    luluMem_free_pointer(vm, ptr, parray_size(ptr, len))

#endif /* LULU_MEMORY_H */
