#ifndef LUA_OBJECT_H
#define LUA_OBJECT_H

#include <stdbool.h>
#include "lua.h"
#include "limits.h"

// Tagged union of garbage-collectible structs: strings, tables, functions, etc.
typedef struct Object Object;

// Tagged union of all possible Lua values: numbers, booleans and objects.
typedef struct lua_TValue {
    union {
        Object *object;    // Contains type information and GC data.
        lua_Number number; // Also used for `nil`, it just has a distinct tag.
        bool boolean;      // Since Lua is written in C89 it uses `int`.
    } as;
    int tag; // Tagged union type. Use one of the `LUA_T*` enum members.
} TValue;

#define tagtype(value)      (value)->tag

// All garbage-collectible tags come after the `LUA_TSTRING` enum member.
// `LUA_TSTRING` itself is garbage collectible so we include it.
#define iscollectible(value)    (tagtype(value) >= LUA_TSTRING)

#define isnil(value)        (tagtype(value) == LUA_TNIL)
#define isboolean(value)    (tagtype(value) == LUA_TBOOLEAN)
#define isnumber(value)     (tagtype(value) == LUA_TNUMBER)
#define isobject(value)     iscollectible(value)
#define isstring(value)     (tagtype(value) == LUA_TSTRING)
#define istable(value)      (tagtype(value) == LUA_TTABLE)
#define isfunction(value)   (tagtype(value) == LUA_TFUNCTION)

#define asboolean(value)    (value)->as.boolean
#define asnumber(value)     (value)->as.number
#define asobject(value)     check_exp(isobject(value),  (value)->as.object)

#define setnil(obj) {                                                          \
    TValue *dst = (obj);                                                       \
    dst->as.number = 0;                                                        \
    dst->tag = LUA_TNIL;                                                       \
}

#define setboolean(obj, b) {                                                   \
    TValue *dst = (obj);                                                       \
    dst->as.boolean = (b);                                                     \
    dst->tag = LUA_TBOOLEAN;                                                   \
}

#define setnumber(obj, n) {                                                    \
    TValue *dst    = (obj);                                                    \
    dst->as.number = (n);                                                      \
    dst->tag = LUA_TNUMBER;                                                    \
}

#define setobj(dst, src) {                                                     \
    TValue *_dst       = (dst);                                                \
    const TValue *_src = (src);                                                \
    _dst->tag = _src->tag;                                                     \
    _dst->as  = _src->as;                                                      \
}

void print_value(const TValue *value);

LUA_API const char *const luaT_typenames[LUA_TCOUNT];

#define astypename(value) luaT_typenames[tagtype(value)]

#endif /* LUA_OBJECT_H */
