#ifndef LULU_MEMORY_H
#define LULU_MEMORY_H

#include "conf.h"

#define grow_capacity(N)    ((N) < 8 ? 8 : (N) * 2)
#define grow_array(T, ptr, oldsz, newsz) \
    reallocate(ptr, arraysize(T, oldsz), arraysize(T, newsz))

#define free_array(T, ptr, len) \
    reallocate(ptr, sizeof(T) * (len), 0)

void *reallocate(void *ptr, size_t oldsz, size_t newsz);

#endif /* LULU_MEMORY_H */
