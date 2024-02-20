#ifndef LUA_VALUE_H
#define LUA_VALUE_H

#include "common.h"

/* Tag for Lua datatypes. */
typedef enum {
    LUA_BOOLEAN,
    LUA_FUNCTION,
    LUA_NIL,
    LUA_NUMBER,
    LUA_STRING,
    LUA_TABLE,
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

/* In memory, `nil` is just a distinct 0. */
#define make_luanil         ((LuaValue){LUA_NIL, {.number = 0.0}})
#define is_luanil(V)        ((V).type == LUA_NIL)

#define make_luanumber(N)   ((LuaValue){LUA_NUMBER, {.number = (N)}})
#define is_luanumber(V)     ((V).type == LUA_NUMBER)
#define as_luanumber(V)     ((V).as.number)

#define make_luaboolean(B)  ((LuaValue){LUA_BOOLEAN, {.boolean = (B)}})
#define is_luaboolean(V)    ((V).type == LUA_BOOLEAN)
#define as_luaboolean(V)    ((V).as.boolean)

#endif /* LUA_VALUE_H */
