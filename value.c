#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define maketypename(s) ((TNameInfo){s, arraylen(s) - 1})

static const TNameInfo typenames[LUA_TCOUNT] = {
    [LUA_TBOOLEAN]  = maketypename("boolean"),
    [LUA_TFUNCTION] = maketypename("function"),
    [LUA_TNIL]      = maketypename("nil"),
    [LUA_TNUMBER]   = maketypename("number"),
    [LUA_TSTRING]   = maketypename("string"),
    [LUA_TTABLE]    = maketypename("table"),
    [LUA_TNONE]     = maketypename("no value"),
};

const TNameInfo *get_tnameinfo(VType tag) {
    return &typenames[(tag >= LUA_TCOUNT) ? LUA_TNONE : tag];
}

void init_valuearray(TArray *self) {
    self->values   = NULL;
    self->count    = 0;
    self->cap = 0;
}

void free_valuearray(TArray *self) {
    deallocate_array(TValue, self->values, self->cap);
    init_valuearray(self);
}

void write_valuearray(TArray *self, const TValue *value) {
    if (self->count + 1 > self->cap) {
        size_t oldcap  = self->cap;
        self->cap    = grow_cap(oldcap);
        self->values = grow_array(TValue, self->values, oldcap, self->cap);
    }
    self->values[self->count] = *value;
    self->count++;
}

void print_value(const TValue *value) {
    switch (value->type) {
    case LUA_TBOOLEAN:  printf(value->as.boolean ? "true" : "false"); break;
    case LUA_TFUNCTION: print_function(asfunction(value)); break;
    case LUA_TNIL:      printf("nil"); break;
    case LUA_TNUMBER:   printf(LUA_NUMBER_FMT, value->as.number); break;
    case LUA_TSTRING:   print_string(asstring(value)); break;
    case LUA_TTABLE:    print_table(astable(value), false); break;
    default:            printf("Unknown type: %i", (int)value->type); break;
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
    case LUA_TSTRING:   // All heap allocated objects are interned.
    case LUA_TFUNCTION:
    case LUA_TTABLE:    return lhs->as.object == rhs->as.object;
    default:            break;
    }
    fprintf(stderr, "Unknown type: %i", (int)lhs->type);
    return false;
}
