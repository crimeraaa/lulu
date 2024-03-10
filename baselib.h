#ifndef LUA_BASELIB_H
#define LUA_BASELIB_H

#include "api.h"
#include "common.h"

/**
 * III:24.7     Native Functions
 *
 * When garbage collection gets involved, it will be important to consider if
 * during the call to `copy_string()` and `new_function` if garbage collection
 * was triggered. If that happens we must tell the GC that we are not actually
 * done with this memory, so storing them on the stack (will) accomplish that
 * when we get to that point.
 * 
 * NOTE:
 * 
 * Since this is called during VM initialization, we can safely assume that the
 * stack is currently empty.
 */
void lua_loadbaselib(LVM *vm);

TValue base_clock(LVM *vm, int argc, TValue *argv);
TValue base_print(LVM *vm, int argc, TValue *argv);
TValue base_type(LVM *vm, int argc, TValue *argv);

#endif /* LUA_BASELIB_H */
