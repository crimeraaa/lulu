#ifndef LUA_BASELIB_H
#define LUA_BASELIB_H

#include "api.h"
#include "common.h"

/**
 * Register the base functions below into the VM. This will intern identifiers
 * are allocate memory for function their objects.
 */
void lua_loadbase(LVM *vm);

#endif /* LUA_BASELIB_H */
