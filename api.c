#include "api.h"
#include "memory.h"
#include "vm.h"

// Placeholder value for invalid stack accesses. Do NOT modify it!
static TValue nilobject = makenil;

/**
 * Convert a positive or negative offset into a pointer to a particular value in
 * the VM's stack. If invalid we return the address of `nilobject` rather than
 * return `NULL` as that'll be terrible.
 *
 * See:
 * - https://www.lua.org/source/5.1/lapi.c.html#index2adr
 */
static TValue *offset_to_address(LVM *self, int offset) {
    if (offset >= 0) {
        // Positive or zero offset in relation to base pointer.
        TValue *value = self->stack + offset;
        return (value >= self->sp) ? &nilobject : value;
    } else {
        // Negative offset in relation to stack pointer.
        return self->sp + offset;
    }
}

bool lua_isfalsy(LVM *self, int offset) {
    size_t i = lua_absindex(self, offset);
    if (lua_isnil(self, i)) {
        return true;
    }
    return lua_isboolean(self, i) && !lua_asboolean(self, i);
}

size_t lua_gettop(const LVM *self) {
    return self->sp - self->stack;
}

void lua_settop(LVM *self, int offset) {
    if (offset >= 0) {
        // Get positive offset in relation to base pointer.
        // Fill gaps with nils.
        while (self->sp < self->stack + offset) {
            *self->sp = makenil;
            self->sp++;
        }
        self->sp = self->stack + offset;
    } else {
        // Is negative offset in relation to stack top pointer.
        self->sp += offset + 1;
    }
}

bool lua_istype(LVM *self, int offset, VType type) {
    return offset_to_address(self, offset)->type == type;
}

VType lua_type(LVM *self, int offset) {
    return offset_to_address(self, offset)->type;
}

const char *lua_typename(LVM *self, int offset) {
    return value_typename(lua_type(self, offset));
}

bool lua_equal(LVM *self, int offset1, int offset2) {
    const TValue *lhs = offset_to_address(self, offset1);
    const TValue *rhs = offset_to_address(self, offset2);
    if (lhs->type != rhs->type) {
        return false;
    }
    switch (lhs->type) {
    case LUA_TNONE:     return false; // Should never happen...
    case LUA_TBOOLEAN:  return lhs->as.boolean == rhs->as.boolean;
    case LUA_TNIL:      return true; // Both nil are always equal.
    case LUA_TNUMBER:   return lhs->as.number == rhs->as.number;
    case LUA_TTABLE:    // All objects are interned so pointer comparisons work.
    case LUA_TFUNCTION:
    case LUA_TNATIVE:
    case LUA_TSTRING:   return lhs->as.object == rhs->as.object;
    default:            break;
    }
    return false;
}

void lua_pushobject(LVM *self, const TValue *object) {
    *self->sp = *object;
    self->sp++;
}

void lua_pushboolean(LVM *self, bool b) {
    TValue v = makeboolean(b);
    lua_pushobject(self, &v);
}

void lua_pushnil(LVM *self) {
    static const TValue v = makenil; // Create only once
    lua_pushobject(self, &v);
}

void lua_pushnumber(LVM *self, lua_Number n) {
    TValue v = makenumber(n);
    lua_pushobject(self, &v);
}

void lua_pushlstring(LVM *self, char *data, size_t len) {
    TValue v = makestring(take_string(self, data, len));
    lua_pushobject(self, &v);
}

void lua_pushstring(LVM *self, char *data) {
    if (data == NULL) {
        lua_pushnil(self);
    } else {
        lua_pushlstring(self, data, strlen(data));
    }
}

void lua_pushliteral(LVM *self, const char *data) {
    TValue v = makestring(copy_string(self, data, strlen(data)));
    lua_pushobject(self, &v);
}

void lua_pushfunction(LVM *self, LFunction *luafn) {
    TValue v = makefunction(luafn);
    lua_pushobject(self, &v);
}

void lua_pushcfunction(LVM *self, CFunction *cfn) {
    TValue v = makecfunction(cfn);
    lua_pushobject(self, &v);
}

void lua_concat(LVM *self) {
    TString *rhs = lua_asstring(self, -1);
    TString *lhs = lua_asstring(self, -2);
    lua_popn(self, 2); // Clean up operands

    size_t len = lhs->len + rhs->len;
    char *data = allocate(char, len + 1);

    memcpy(&data[0],        lhs->data, lhs->len);
    memcpy(&data[lhs->len], rhs->data, rhs->len);
    data[len] = '\0';
    lua_pushlstring(self, data, len);
}
