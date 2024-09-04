#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"
#include "limits.h"
#include "object.h"

#define luluMem_grow_capacity(N)    ((N) < 8 ? 8 : (N) * 2)
#define MEMORY_ERROR_MESSAGE        "[FATAL ERROR]: out of memory"
#define MAX_ALLOCATION              (SIZE_MAX >> 2)

typedef lulu_Allocator Allocator;

void *luluMem_call_allocator(lulu_VM *vm, void *ptr, size_t oldsz, size_t newsz);

Object *luluObj_new(lulu_VM *vm, size_t size, VType tag);
Object *luluObj_link(lulu_VM *vm, Object *obj);
Object *luluObj_unlink(lulu_VM *vm, Object *obj);
void    luluObj_free_all(lulu_VM *vm);

#define luluMem_new_pointer(vm, sz) \
    luluMem_call_allocator(vm, nullptr, 0, sz)

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
