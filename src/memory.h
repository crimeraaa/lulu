#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"
#include "limits.h"
#include "object.h"

#define luluMem_grow_capacity(N)    ((N) < 8 ? 8 : (N) * 2)

typedef void *(*lulu_AllocFn)(void *ptr, size_t oldsz, size_t newsz, void *ctx);

// A general purpose allocation wrapper that carries some context around.
// See: https://nullprogram.com/blog/2023/12/17/
struct lulu_Allocator {
    lulu_AllocFn allocate; // To free `ptr`, pass `newsz` of `0`.
    void        *context;  // How this is interpreted is up to your function.
};

void luluMem_set_allocator(lulu_VM *vm, lulu_AllocFn fn, void *ctx);
void *luluMem_new_pointer(lulu_VM *vm, size_t size);
void *luluMem_resize_pointer(lulu_VM *vm, void *ptr, size_t oldsz, size_t newsz);
void  luluMem_free_pointer(lulu_VM *vm, void *ptr, size_t size);

Object *luluObj_new(lulu_VM *vm, size_t size, VType tag);
void luluObj_free_all(lulu_VM *vm);

// Pushes `obj` to the top of the VM's object linked list. Returns `obj`.
Object *luluObj_prepend(lulu_VM *vm, Object *obj);

// Unlink `obj` from the VM's linked list of objects. Returns `obj`.
Object *luluObj_remove(lulu_VM *vm, Object *obj);

#define luluMem_new_array(vm, T, len) \
    luluMem_new_pointer(vm, array_size(T, len))

#define luluMem_resize_array(vm, T, ptr, oldcap, newcap) \
    luluMem_resize_pointer(vm, ptr, array_size(T, oldcap), array_size(T, newcap))

#define luluMem_free_array(vm, T, ptr, len) \
    luluMem_free_pointer(vm, ptr, array_size(T, len))

#define luluMem_new_parray(vm, ptr, len) \
    luluMem_new_pointer(vm, parray_size(ptr, len))

#define luluMem_resize_parray(vm, ptr, oldcap, newcap) \
    luluMem_resize_pointer(vm, ptr, parray_size(ptr, oldcap), parray_size(ptr, newcap))

#define luluMem_free_parray(vm, ptr, len) \
    luluMem_free_pointer(vm, ptr, parray_size(ptr, len))

#endif /* LULU_MEMORY_H */
