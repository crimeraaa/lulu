#include "api.h"
#include "memory.h"
#include "vm.h"

#undef lua_pushliteral  /* Undef user-facing macro for implementation. */

/**
 * We use a temporary/local variable in case `ptr` itself has side effects.
 * 
 * @param ptr   Pointer to the `TValue` in the stack to be modified.
 * @param v     Value to be assigned.
 * @param tt    Type tag (`ValueType`) to be used to maintain the tagged union.
 * @param memb  C source code token. One of `boolean`, `number` or `object`.
 */
#define setvalue(ptr, v, tt, memb) do {                                        \
        TValue *_ptr  = ptr;                                                   \
        _ptr->type    = tt;                                                    \
        _ptr->as.memb = v;                                                     \
    } while (false)

#define setboolean(ptr, b)      setvalue(ptr,  b, LUA_TBOOLEAN, boolean)
#define setnil(ptr)             setvalue(ptr,  0, LUA_TNIL,     number)
#define setnumber(ptr, n)       setvalue(ptr,  n, LUA_TNUMBER,  number)
#define setobject(ptr, o, tt)   setvalue(ptr,  (lua_Object*)o, tt,object)
#define setstring(ptr, s)       setobject(ptr, s, LUA_TSTRING)

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
static TValue *offset_to_address(lua_VM *self, int offset) {
    if (offset >= 0) {
        // Positive or zero offset in relation to base pointer.
        TValue *value = self->bp + offset;
        return (value >= self->sp) ? &nilobject : value;
    } else {
        // Negative offset in relation to stack pointer.
        return self->sp + offset;
    }
}

bool lua_isfalsy(lua_VM *self, int offset) {
    size_t i = lua_absindex(self, offset);
    if (lua_isnil(self, i)) {
        return true;
    }
    return lua_isboolean(self, i) && !lua_asboolean(self, i);
}

void lua_settop(lua_VM *self, int offset) {
    if (offset >= 0) {
        // Get positive offset in relation to base pointer.
        // Fill gaps with nils.
        while (self->sp < self->bp + offset) {
            *self->sp = makenil;
            self->sp++;
        }
        self->sp = self->bp + offset;
    } else {
        // Is negative offset in relation to stack top pointer.
        self->sp += offset + 1; 
    }
}

bool lua_istype(lua_VM *self, int offset, ValueType type) {
    return offset_to_address(self, offset)->type == type;
}

const char *lua_typename(lua_VM *self, int offset) {
    switch (offset_to_address(self, offset)->type) {
    case LUA_TNONE:     return "none";    // Should never be reached normally.
    case LUA_TBOOLEAN:  return "boolean";
    case LUA_TFUNCTION: return "function";
    case LUA_TNIL:      return "nil";
    case LUA_TNUMBER:   return "number";
    case LUA_TSTRING:   return "string";
    case LUA_TTABLE:    return "table";
    default:            return "unknown"; // Should never be reached normally.
    }
}

bool lua_equal(lua_VM *self, int offset1, int offset2) {
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
    case LUA_TSTRING:   return lhs->as.object == rhs->as.object;
    default:            break;
    }
    return false;
}

static void copy_values(TValue *dst, const TValue *src) {
    switch (src->type) {
    case LUA_TNONE:     return; // Should never happen normally.
    case LUA_TBOOLEAN:  setboolean(dst, src->as.boolean); break;
    case LUA_TNIL:      setnil(dst); break;
    case LUA_TNUMBER:   setnumber(dst, src->as.number); break;
    case LUA_TTABLE:
    case LUA_TFUNCTION:
    case LUA_TSTRING:   setstring(dst, src->as.object); break;
    default:            break;
    }
}

void lua_pushconstant(lua_VM *self, size_t i) {
    TValue *dst = self->sp++;
    const TValue *src = &lua_getconstant(self, i);
    copy_values(dst, src);
}

void lua_pushboolean(lua_VM *self, bool b) {
    setboolean(self->sp++, b);
}

void lua_pushnil(lua_VM *self) {
    setnil(self->sp++);
}

void lua_pushnumber(lua_VM *self, lua_Number n) {
    setnumber(self->sp++, n);
}

void lua_pushlstring(lua_VM *self, char *data, size_t len) {
    lua_String *s = take_string(self, data, len);
    setstring(self->sp++, s);
}

void lua_pushstring(lua_VM *self, char *data) {
    if (data == NULL) {
        lua_pushnil(self);
    } else {
        lua_pushlstring(self, data, strlen(data));
    }
}

void lua_pushliteral(lua_VM *self, const char *data, size_t len) {
    lua_String *s = copy_string(self, data, len);
    setstring(self->sp++, s);
}

void lua_concat(lua_VM *self) {
    lua_String *rhs = lua_asstring(self, -1);
    lua_String *lhs = lua_asstring(self, -2);
    lua_popn(self, 2); // Clean up operands

    size_t len = lhs->len + rhs->len;
    char *data = allocate(char, len + 1);

    memcpy(&data[0],        lhs->data, lhs->len);
    memcpy(&data[lhs->len], rhs->data, rhs->len);
    data[len] = '\0';
    lua_pushlstring(self, data, len);
}
