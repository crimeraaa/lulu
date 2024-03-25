#ifndef LUA_H
#define LUA_H

#include "conf.h"

typedef struct lua_VM lua_VM;

enum {
    LUA_TNONE = -1,
    LUA_TNIL,
    LUA_TBOOLEAN,
    LUA_TNUMBER,
    LUA_TSTRING,
    LUA_TTABLE,
    LUA_TFUNCTION,
};

#endif /* LUA_H */
