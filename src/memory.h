#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"
#include "limits.h"

#define allocate(vm, T, N) \
    reallocate(vm, NULL, 0, arraysize(T, N))

#define deallocate(vm, T, ptr) \
    reallocate(vm, ptr, sizeof(T), 0)

#define deallocate_flexarray(vm, ST, MT, N, ptr) \
    reallocate(vm, ptr, flexarray_size(ST, MT, N), 0)

// +1 was allocated for the nul char.
#define deallocate_tstring(vm, ptr) \
    deallocate_flexarray(vm, TString, char, (ptr)->len + 1, ptr)

#define grow_capacity(N)    ((N) < 8 ? 8 : (N) * 2)
#define grow_array(vm, T, ptr, oldcap, newcap) \
    reallocate(vm, ptr, arraysize(T, oldcap), arraysize(T, newcap))

#define free_array(vm, T, ptr, len) \
    reallocate(vm, ptr, arraysize(T, len), 0)

void *reallocate(VM *vm, void *ptr, size_t oldsz, size_t newsz);
void free_objects(VM *vm);

#endif /* LULU_MEMORY_H */
