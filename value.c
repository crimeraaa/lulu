#include <ctype.h>
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

void init_tarray(TArray *self) {
    self->array   = NULL;
    self->count    = 0;
    self->cap = 0;
}

void free_tarray(TArray *self) {
    deallocate_array(TValue, self->array, self->cap);
    init_tarray(self);
}

void write_tarray(TArray *self, const TValue *value) {
    if (self->count + 1 > self->cap) {
        size_t oldcap = self->cap;
        self->cap     = grow_cap(oldcap);
        self->array   = grow_array(TValue, self->array, oldcap, self->cap);
    }
    self->array[self->count] = *value;
    self->count++;
}

void print_value(const TValue *value) {
    char buf[LUA_MAXNUM2STR];
    const char *out;
    check_tostring(value, buf, &out);
    printf("%s", out);
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

int check_tostring(const TValue *v, char *buf, const char **out) {
    int len = 0;
    *out = NULL;
    switch (v->type) {
    case LUA_TBOOLEAN:
        *out = asboolean(v) ? "true" : "false";
        return 0;
    case LUA_TNIL:
        *out = "nil";
        return 0;
    case LUA_TNUMBER:
        len = lua_num2str(buf, asnumber(v));
        break;
    case LUA_TSTRING:
        // Can't safely assume we can write to the fixed-size `buf`.
        *out = ascstring(v);
        return 0;
    default: {
        // User should not be able to query the top-level script itself though!
        if (isluafunction(v) && asluafunction(v).name == NULL) {
            *out = "(script)";
            return 0;
        }
        const char *s = get_tnameinfo(v->type)->what;
        const void *p = asobject(v);
        len = snprintf(buf, LUA_MAXNUM2STR, "%s: %p", s, p);
    } break;
    }
    buf[len] = '\0';
    *out = buf;
    return len;
}

bool check_tonumber(const char *source, lua_Number *result) {
    char *endptr;
    *result = lua_str2num(source, &endptr);
    if (endptr == source) {
        return false; // Conversion failed since endptr is just the start.
    }
    if (*endptr == 'x' || *endptr == 'X') {
        // Maybe a hexidecimal constant.
        *result = (lua_Number)strtoul(source, &endptr, 16);
    }
    if (*endptr == '\0') {
        return true;
    }
    while (isspace(*endptr)) {
        endptr++;
    }
    if (*endptr != '\0') {
        return false; // Invalid trailing characters.
    }
    return true;
}
