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
    case LUA_BOOLEAN: printf(value.as.boolean ? "true" : "false"); break;
    case LUA_NIL:     printf("nil"); break;
    case LUA_NUMBER:  printf("%g", value.as.number); break;
    default:          printf("Unknown type %i", (int)value.type);
    }
}
