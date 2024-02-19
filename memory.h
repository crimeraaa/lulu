#ifndef LUA_MEMORY_H
#define LUA_MEMORY_H

#include "common.h"

/**
 * Helper macro to make growingdynamic arrays quicker.
 * We start arrays with 8 elements and grow by factors of 2.
 */
#define grow_capacity(N)    ((N) < 8 ? 8 : (N) * 2)

#define grow_array(T, Pointer, OldCapacity, NewCapacity) \
    reallocate(Pointer, sizeof(T[OldCapacity]), sizeof(T[NewCapacity]))

#define deallocate_array(T, Pointer, Capacity) \
    reallocate(Pointer, sizeof(T[Capacity]), 0)

/**
 * @brief   Crafting Interpreters Part III, Chapter 14.3.1
 *          Handles all our dynamic memory management.
 *          
 * @param pointer   If NULL, we allocate a new memory block of `newsize`.
 * @param oldsize   Previous byte allocation count of `pointer`.
 *                  For now this is unused because the C standard `malloc` already
 *                  keeps track of how many bytes a particular pointer is using.
 * @param newsize   Requested byte allocation count for `pointer`.
 *                  If 0, we free `pointer`. Otherwise we call `realloc`.
 *                  
 * @note    Due to the nature of `realloc` and the implementation of the `malloc`
 *          family, we don't actually need `oldsize` for anything since the 
 *          implementation does that kind of bookkeeping for us already.
 */
void *reallocate(void *pointer, size_t oldsize, size_t newsize);

#endif /* LUA_MEMORY_H */
