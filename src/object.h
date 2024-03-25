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

#define tagtype(o)          ((o)->tag)

// All garbage-collectible tags come after the `LUA_TSTRING` enum member.
#define iscollectible(o)    (tagtype(o) >= LUA_TSTRING)

#define isnil(o)            (tagtype(o) == LUA_TNIL)
#define isboolean(o)        (tagtype(o) == LUA_TBOOLEAN)
#define isnumber(o)         (tagtype(o) == LUA_TNUMBER)
#define isstring(o)         (tagtype(o) == LUA_TSTRING)
#define istable(o)          (tagtype(o) == LUA_TTABLE)
#define isfunction(o)       (tagtype(o) == LUA_TFUNCTION)

#define asboolean(o)        check_exp(isboolean(o),     (o)->as.boolean)
#define asnumber(o)         check_exp(isnumber(o),      (o)->as.number)
#define asobject(o)         check_exp(iscollectible(o), (o)->as.object)

#define setnil(obj) \
    { TValue *_o = (obj); _o->as.number = 0; _o->tag = LUA_TNIL; }

#define setboolean(obj, b) \
    { TValue *_o = (obj); _o->as.boolean = (b); _b->tag = LUA_TBOOLEAN; }

#define setnumber(obj, n) \
    { TValue *_o = (obj); _o->as.number = (n); _o->tag = LUA_TNUMBER; }

void print_value(const TValue *value);

extern const char *const luaT_typenames[LUA_TCOUNT];

#endif /* LUA_OBJECT_H */
