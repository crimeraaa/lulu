#include "memory.h"
#include "value.h"

void init_valuearray(ValueArray *self) {
    self->values   = NULL;
    self->count    = 0;
    self->capacity = 0;
}

void deinit_valuearray(ValueArray *self) {
    deallocate_array(TValue, self->values, self->capacity);
    init_valuearray(self);
}

void write_valuearray(ValueArray *self, TValue value) {
    if (self->count + 1 > self->capacity) {
        int oldcapacity = self->capacity;
        self->capacity  = grow_capacity(oldcapacity);
        self->values    = grow_array(TValue, self->values, oldcapacity, self->capacity);
    }
    self->values[self->count] = value;
    self->count++;
}

void print_value(TValue value) {
    switch (value.type) {
    case LUA_TBOOLEAN: printf(value.as.boolean ? "true" : "false"); break;
    case LUA_TNIL:     printf("nil"); break;
    case LUA_TNUMBER:  printf(LUA_NUMBER_FMT, value.as.number); break;
    default:           printf("Unsupported type %s", lua_typename(value.type));
    }
}

const char *lua_typename(ValueType type) {
    switch (type) {
    case LUA_TBOOLEAN:   return "boolean";
    case LUA_TNIL:       return "nil";
    case LUA_TNUMBER:    return "number";
    }
}

bool values_equal(TValue lhs, TValue rhs) {
    if (lhs.type != rhs.type) {
        return false;
    }
    // If above test passed, we can assume they ARE the same type
    switch (lhs.type) {
    case LUA_TBOOLEAN:  return lhs.as.boolean == rhs.as.boolean;
    case LUA_TNIL:      return true; // nil is always == nil.
    case LUA_TNUMBER:   return lhs.as.number == rhs.as.number;
    default:            break;
    }
    fprintf(stderr, "Unsupported type %s.\n", lua_typename(lhs.type));
    return false;
}
