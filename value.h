#ifndef LUA_VALUE_H
#define LUA_VALUE_H

#include "common.h"

/* Tag for Lua datatypes. */
typedef enum {
    LUA_TBOOLEAN,
    LUA_TFUNCTION,
    LUA_TNIL,
    LUA_TNUMBER,
    LUA_TSTRING,
    LUA_TTABLE,
} LuaValueType;

/* Tagged union for Lua's fundamental datatypes. */
typedef struct {
    LuaValueType type; // Tag for the union to ensure some type safety.
    union {
        bool boolean;
        double number;
    } as; // Actual value contained within this struct. Be very careful!
} LuaValue;

/* LuaValue array. */
typedef struct {
    LuaValue *values; // 1D array of Lua values.
    int count;
    int capacity;
} LuaValueArray;

void init_valuearray(LuaValueArray *self);
void write_valuearray(LuaValueArray *self, LuaValue value);
void deinit_valuearray(LuaValueArray *self);
void print_value(LuaValue value);

/**
 * III:18.4.2   Equality and comparison operators
 * 
 * Given 2 dynamically typed values, how do we compare them? Well, if they're of
 * different types, you can automatically assume they're not the same.
 * 
 * Otherwise, we'll need to do a comparison on a type-by-type basis.
 * 
 * NOTE:
 * 
 * We CANNOT use memcmp as it's likely the compiler added padding, which goes
 * unused. If we do raw memory comparisons we'll also compare these garbage bits
 * which will not give us the results we want.
 */
bool values_equal(LuaValue lhs, LuaValue rhs);

/**
 * III:17.7     Dumping Chunks (my addition)
 * 
 * Since Lua has only a few possible datatypes and user-defined ones are always
 * going to be `table`, we can easily hardcode these.
 * 
 * In Lua the `type()` function returns a string literal showing what datatype a
 * particular value is.
 */
const char *typeof_value(LuaValue value);

/* In memory, `nil` is just a distinct 0. */
#define make_luanil         ((LuaValue){LUA_TNIL, {.number = 0.0}})
#define is_luanil(V)        ((V).type == LUA_TNIL)

#define make_luanumber(N)   ((LuaValue){LUA_TNUMBER, {.number = (N)}})
#define is_luanumber(V)     ((V).type == LUA_TNUMBER)
#define as_luanumber(V)     ((V).as.number)

#define make_luaboolean(B)  ((LuaValue){LUA_TBOOLEAN, {.boolean = (B)}})
#define is_luaboolean(V)    ((V).type == LUA_TBOOLEAN)
#define as_luaboolean(V)    ((V).as.boolean)

#endif /* LUA_VALUE_H */
