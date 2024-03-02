#ifndef LUA_MEMORY_H
#define LUA_MEMORY_H

#include "common.h"

/**
 * Helper wrapper to make allocating new blocks of memory easier.
 */
#define allocate(T, count)      reallocate(NULL, 0, sizeof(T[count]))

/**
 * III:19.5     Freeing Objects
 * 
 * Helper wrapper to make deallocating single instances of particular types
 * easier.
 */
#define deallocate(T, pointer)  reallocate(pointer, sizeof(T), 0)

/**
 * Helper macro to make growingdynamic arrays quicker.
 * We start arrays with 8 elements and grow by factors of 2.
 */
#define grow_cap(N)        ((N) < 8 ? 8 : (N) * 2)

#define grow_array(T, ptr, oldcap, newcap) \
    reallocate(ptr, sizeof(T[oldcap]), sizeof(T[newcap]))

/**
 * Helper macro to make deallocating memory of array types easier.
 */
#define deallocate_array(T, ptr, cap)    reallocate(ptr, sizeof(T[cap]), 0)

/**
 * @brief   Crafting Interpreters Part III, Chapter 14.3.1
 *          Handles all our dynamic memory management.
 *          
 * @param pointer If NULL, we allocate a new memory block of `newsz`.
 * @param oldsz   Previous byte allocation count of `pointer`.
 *                For now this is unused because C standard `malloc` already
 *                keeps track of how many bytes a particular pointer is using.
 * @param newsz   Requested byte allocation count for `pointer`.
 *                If 0, we free `pointer`. Otherwise we call `realloc`.
 *                  
 * @note    Due to the nature of `realloc` and the implementation of `malloc`
 *          family, we don't actually need `oldsz` for anything since the 
 *          implementation does that kind of bookkeeping for us already.
 */
void *reallocate(void *pointer, Size oldsz, Size newsz);

/**
 * III:19.5     Freeing Objects
 * 
 * Pass in a VM pointer with which we'll walk to instrusive linked list of.
 * We'll deallocate the memory alloted for each object, assuming all is well.
 */
void free_objects(lua_VM *lvm);

#endif /* LUA_MEMORY_H */
