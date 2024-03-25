#ifndef LUA_MEMORY_H
#define LUA_MEMORY_H

#include "lua.h"

#define grow_capacity(n)    ((n) < 8 ? 8 : (n) * 2)
#define grow_array(T, ptr, oldn, newn) \
    (T*)reallocate(ptr, sizeof(T) * (oldn), sizeof(T) * (newn))

#define free_array(T, ptr, N) \
    reallocate(ptr, sizeof(T) * (N), 0)

// May call `exit(1)` if realloc returns NULL.
void *reallocate(void *ptr, size_t oldsz, size_t newsz);

#endif /* LUA_MEMORY_H */
