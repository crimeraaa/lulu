#include "memory.h"
#include "object.h"
#include "value.h"

void init_valuearray(ValueArray *self) {
    self->values   = NULL;
    self->count    = 0;
    self->cap = 0;
}

void free_valuearray(ValueArray *self) {
    deallocate_array(TValue, self->values, self->cap);
    init_valuearray(self);
}

void write_valuearray(ValueArray *self, TValue value) {
    if (self->count + 1 > self->cap) {
        size_t oldcap  = self->cap;
        self->cap    = grow_cap(oldcap);
        self->values = grow_array(TValue, self->values, oldcap, self->cap);
    }
    self->values[self->count] = value;
    self->count++;
}

void print_value(const TValue *value) {
    switch (value->type) {
    case LUA_TBOOLEAN: printf(value->as.boolean ? "true" : "false"); break;
    case LUA_TNIL:     printf("nil"); break;
    case LUA_TNUMBER:  printf(LUA_NUMBER_FMT, value->as.number); break;
    case LUA_TSTRING:  printf("%s", ascstring(*value)); break;
    default:           printf("Unsupported type %s", value_typename(value)); break;
    }
}

const char *value_typename(const TValue *value) {
    switch (value->type) {
    case LUA_TBOOLEAN:      return "boolean";
    case LUA_TFUNCTION:     return "function";
    case LUA_TNIL:          return "nil";
    case LUA_TNUMBER:       return "number";
    case LUA_TSTRING:       return "string";
    case LUA_TTABLE:        return "table";
    default:                return "unknown"; // Fallback
    } 
}

bool values_equal(const TValue *lhs, const TValue *rhs) {
    if (lhs->type != rhs->type) {
        return false;
    }
    // If above test passed, we can assume they ARE the same type
    switch (lhs->type) {
    case LUA_TBOOLEAN:  return lhs->as.boolean == rhs->as.boolean;
    case LUA_TNIL:      return true; // nil is always == nil.
    case LUA_TNUMBER:   return lhs->as.number == rhs->as.number;
    // Strings are interned so this should be ok. Not sure about other types.
    case LUA_TSTRING:
    case LUA_TFUNCTION:
    case LUA_TTABLE:    return lhs->as.object == rhs->as.object;
    default:            break;
    }
    fprintf(stderr, "Unsupported type %s.\n", value_typename(lhs));
    return false;
}
