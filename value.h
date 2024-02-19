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

/* Tagged union (hence suffix 'U') for Lua datatypes. */
typedef struct {
    LuaValueType type; 
    union {
        bool boolean;
        double number;
    } as;
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

#define make_number(N)  ((LuaValue){LUA_NUMBER, {.number = (N)}})

#endif /* LUA_VALUE_H */
