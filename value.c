#include "memory.h"
#include "value.h"

void init_valuearray(LuaValueArray *self) {
    self->values   = NULL;
    self->count    = 0;
    self->capacity = 0;
}

void deinit_valuearray(LuaValueArray *self) {
    deallocate_array(LuaValue, self->values, self->capacity);
    init_valuearray(self);
}

void write_valuearray(LuaValueArray *self, LuaValue value) {
    if (self->count + 1 > self->capacity) {
        int oldcapacity = self->capacity;
        self->capacity  = grow_capacity(oldcapacity);
        self->values    = grow_array(LuaValue, self->values, oldcapacity, self->capacity);
    }
    self->values[self->count] = value;
    self->count++;
}

void print_value(LuaValue value) {
    switch (value.type) {
    case LUA_TBOOLEAN: printf(value.as.boolean ? "true" : "false"); break;
    case LUA_TNIL:     printf("nil"); break;
    case LUA_TNUMBER:  printf("%g", value.as.number); break;
    default:           printf("Unsupported type %s", typeof_value(value));
    }
}

const char *typeof_value(LuaValue value) {
    switch (value.type) {
    case LUA_TBOOLEAN:   return "boolean";
    case LUA_TFUNCTION:  return "function";
    case LUA_TNIL:       return "nil";
    case LUA_TNUMBER:    return "number";
    case LUA_TSTRING:    return "string";
    case LUA_TTABLE:     return "table";
    }
}
