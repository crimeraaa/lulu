#ifndef LUA_H
#define LUA_H

#include "conf.h"

typedef struct lua_VM lua_VM;

enum {
    LUA_TNONE = -1, // An invalid datatype, not meant to be user-facing.
    LUA_TNIL,
    LUA_TBOOLEAN,
    LUA_TNUMBER,
    LUA_TSTRING,
    LUA_TTABLE,
    LUA_TFUNCTION,
    LUA_TCOUNT, // Not a real tag-type, count of valid types.
};

typedef LUA_NUMBER lua_Number;

#endif /* LUA_H */
