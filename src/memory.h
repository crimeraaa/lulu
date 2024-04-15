#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "lulu.h"
#include "limits.h"

#define allocate(T, N) \
    reallocate(NULL, 0, arraysize(T, N))

#define deallocate(T, ptr) \
    reallocate(ptr, sizeof(T), 0)

#define grow_capacity(N)    ((N) < 8 ? 8 : (N) * 2)
#define grow_array(T, ptr, oldsz, newsz) \
    reallocate(ptr, arraysize(T, oldsz), arraysize(T, newsz))

#define free_array(T, ptr, len) \
    reallocate(ptr, sizeof(T) * (len), 0)

void *reallocate(void *ptr, size_t oldsz, size_t newsz);
void free_objects(VM *vm);

#endif /* LULU_MEMORY_H */
