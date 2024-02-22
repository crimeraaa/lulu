#ifndef LUA_VALUE_H
#define LUA_VALUE_H

#include "common.h"
#include "conf.h"

/* The tag part of the tagged union `TValue`. */
typedef enum {
    LUA_TBOOLEAN,
    LUA_TFUNCTION,
    LUA_TNIL,
    LUA_TNUMBER,
    LUA_TSTRING,
    LUA_TTABLE,
} ValueType;

typedef LUA_NUMBER lua_Number;

/* The union part of the tagged union `TValue`. Do NOT use this as is! */
typedef union {
    bool boolean;
    lua_Number number;
} Value;

/* Tagged union for Lua's fundamental datatypes. */
typedef struct {
    ValueType type; // Tag for the union to ensure some type safety.
    Value as; // Actual value contained within this struct. Be very careful!
} TValue;

/* TValue array. */
typedef struct {
    TValue *values; // 1D array of Lua values.
    int count;
    int capacity;
} ValueArray;

void init_valuearray(ValueArray *self);
void write_valuearray(ValueArray *self, TValue value);
void deinit_valuearray(ValueArray *self);
void print_value(TValue value);

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
bool values_equal(TValue lhs, TValue rhs);

/**
 * III:17.7     Dumping Chunks (my addition)
 * 
 * Since Lua has only a few possible datatypes and user-defined ones are always
 * going to be `table`, we can easily hardcode these.
 * 
 * In Lua the `type()` function returns a string literal showing what datatype a
 * particular value is.
 * 
 * NOTE:
 * 
 * In the Lua C API, `lua_type()` returns an `int` which contains bit information
 * about the type. `lua_typename()` returns the string representation of it.
 * 
 * In our implementation, we follow Bob's method of using a dedicated tag type.
 * 
 * See: https://www.lua.org/source/5.1/lapi.c.html#lua_type
 */
const char *lua_typename(ValueType type);

/* In memory, `nil` is just a distinct 0. */
#define makenil         ((TValue){LUA_TNIL, {.number = 0.0}})
#define isnil(V)        ((V).type == LUA_TNIL)

#define makenumber(N)   ((TValue){LUA_TNUMBER, {.number = (N)}})
#define isnumber(V)     ((V).type == LUA_TNUMBER)
#define asnumber(V)     ((V).as.number)

#define makeboolean(B)  ((TValue){LUA_TBOOLEAN, {.boolean = (B)}})
#define isboolean(V)    ((V).type == LUA_TBOOLEAN)
#define asboolean(V)    ((V).as.boolean)

#endif /* LUA_VALUE_H */
