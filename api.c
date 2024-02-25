#include "api.h"
#include "object.h"
#include "value.h"
#include "vm.h"

/* LUA STACK MANIPULATION ----------------------------------------------- {{{ */

/**
 * Helper to return a pointer to the stack element at the given offset/offset.
 * Unlike the official Lua implementation, we only ever use negative offsets in
 * relation to the stack pointer.
 */
static inline TValue *lua_offset_to_address(lua_VM *self, int offset) {
    // Point to some valid element, hopefully
    TValue *value = self->sp - 1 - offset;
    return (value >= self->bp && value < self->sp) ? value : NULL;
}

int lua_gettop(lua_VM *self) {
    return (int)(self->sp - self->bp);
}

TValue lua_settop(lua_VM *self, int offset) {
    if (offset >= 0) {
        // To 'pop' elements (such as in `lua_popvalues()`), we simply make them nil.
        while (self->sp < self->bp + offset) {
            *self->sp = makenil;
            self->sp++;
        }
        // Now stack pointer points to desired new top of the stack.
        self->sp = self->bp + offset;
    } else {
        // If offset is negative, we subtract it and add 1 to get to the next
        // free element. Remember we don't do much cleanup for the stack array.
        self->sp += offset + 1;
    }
    return *self->sp;
}

void lua_setobj(lua_VM *self, TValue *dst, const TValue *src) {
    dst->type = src->type;
    dst->as = src->as;
}

ValueType lua_type(lua_VM *self, int offset) {
    const TValue *slot = lua_offset_to_address(self, offset);
    // Compare addresses to ensure given object is in the current stack frame.
    if (slot >= self->bp && slot < self->sp) {
        return slot->type;
    }
    return LUA_TNONE;
}

TValue *lua_pokevalue(lua_VM *self, int offset) {
    return lua_offset_to_address(self, offset);
}

TValue lua_peekvalue(lua_VM *self, int offset) {
    TValue *slot = lua_pokevalue(self, offset);
    return (slot != NULL) ? *slot : makenil;
}

/* }}} */

Byte lua_readbyte(lua_VM *self) {
    Byte byte = *self->ip;
    self->ip++;
    return byte;
}

DWord lua_readdword(lua_VM *self) {
    Byte hi  = lua_readbyte(self); // bits 16..23 : (0x010000..0xFFFFFF)
    Byte mid = lua_readbyte(self); // bits 8..15  : (0x000100..0x00FFFF)
    Byte lo  = lua_readbyte(self); // bits 0..7   : (0x000000..0x0000FF)
    return (hi >> 16) | (mid >> 8) | (lo);
}

TValue lua_readconstant_long(lua_VM *self) {
    return self->chunk->constants.values[lua_readdword(self)];
}

TValue lua_readconstant(lua_VM *self) {
    return self->chunk->constants.values[lua_readbyte(self)];
}

lua_String *lua_readstring(lua_VM *self) {
    return (lua_String*)(lua_readconstant(self).as.object);
}

lua_String *lua_readstring_long(lua_VM *self) {
    return (lua_String*)(lua_readconstant_long(self).as.object);
}

/* LUA STACK PUSH (C->STACK) -------------------------------------------- {{{ */

void lua_pushboolean(lua_VM *self, bool b) {
    *self->sp = makeboolean(b);
    self->sp++;
}

void lua_pushconstant(lua_VM *self) {
    *self->sp = lua_readconstant(self);
    self->sp++;
}

void lua_pushconstant_long(lua_VM *self) {
    *self->sp = lua_readconstant_long(self);
    self->sp++;
}

void lua_pushnil(lua_VM *self) {
    *self->sp = makenil;
    self->sp++;
}

void lua_pushnumber(lua_VM *self, lua_Number n) {
    *self->sp = makenumber(n);
    self->sp++;
}

void lua_pushvalue(lua_VM *self, TValue value) {
    lua_setobj(self, self->sp, &value);
    self->sp++;
}

/* }}} */

const char *lua_typename(lua_VM *self, int type) {
    (void)self;
    switch (type) {
    case LUA_TNONE:     return "no value";
    case LUA_TBOOLEAN:  return "boolean";
    case LUA_TFUNCTION: return "function";
    case LUA_TNIL:      return "nil";
    case LUA_TNUMBER:   return "number";
    case LUA_TSTRING:   return "string";
    case LUA_TTABLE:    return "table";
    default:            return "unknown";
    }
}

void lua_print(lua_VM *self) {
    TValue value = lua_popvalue(self);
    switch (value.type) {
    case LUA_TBOOLEAN:
        fputs(value.as.boolean ? "true" : "false", stdout);
        break;
    case LUA_TNIL:
        fputs("nil", stdout);
        break;
    case LUA_TSTRING:
        fputs(asstring(value)->data, stdout);
        break;
    case LUA_TNUMBER:
        fprintf(stdout, LUA_NUMBER_FMT, value.as.number);
        break;
    default:
        fprintf(stdout, "Unsupported type '%s'.", lua_typename(self, value.type));
        break;
    }
    fputs("\n", stdout);
}
