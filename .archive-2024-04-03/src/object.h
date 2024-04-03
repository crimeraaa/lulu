#ifndef LUA_OBJECT_H
#define LUA_OBJECT_H

#include <stdbool.h>
#include "lua.h"
#include "limits.h"

// Not a real tag-type, only meant as a count of valid types.
#define NUM_TYPETAGS        (LUA_TFUNCTION + 1)

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

#define makenil()           (TValue){.as = {.number  = 0},  .tag = LUA_TNIL}
#define makeboolean(b)      (TValue){.as = {.boolean = b},  .tag = LUA_TBOOLEAN}
#define makenumber(n)       (TValue){.as = {.number  = n},  .tag = LUA_TNUMBER}
#define makeobject(o, tt)   (TValue){.as = {.object  = o},  .tag = tt}

#define setnil(obj)         (*(obj) = makenil())
#define setboolean(obj, b)  (*(obj) = makeboolean(b))
#define setnumber(obj, n)   (*(obj) = makenumber(n))
#define setobj(dst, src)    (*(dst) = *(src))

void print_value(const TValue *value);

extern const char *const LUA_TYPENAMES[NUM_TYPETAGS];

#define astypename(value) LUA_TYPENAMES[tagtype(value)]

#endif /* LUA_OBJECT_H */
