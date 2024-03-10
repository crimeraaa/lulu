#include "api.h"
#include "memory.h"
#include "vm.h"

// Placeholder value for invalid stack accesses. Do NOT modify it!
static TValue noneobject = {.type = LUA_TNONE, .as = {.number = 0}};
static TValue nilobject  = makenil;

TValue *lua_poke(LVM *self, int offset) {
    if (offset >= 0) {
        // Positive or zero offset in relation to base pointer, which may not
        // necessarily point to the bottom of the stack.
        TValue *value = self->bp + offset;
        return (value >= self->sp) ? &noneobject : value;
    } else {
        // Negative offset in relation to stack pointer.
        return self->sp + offset;
    }
}

size_t lua_gettop(LVM *self) {
    return self->sp - self->stack;
}

void lua_settop(LVM *self, int offset) {
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

VType lua_type(LVM *self, int offset) {
    return lua_poke(self, offset)->type;
}

const char *lua_typename(LVM *self, VType type) {
    (void)self;
    return get_tnameinfo(type)->what;
}

/* 'IS' FUNCTIONS ------------------------------------------------------- {{{ */

bool lua_iscfunction(LVM *self, int offset) {
    const TValue *v = lua_poke(self, offset);
    return isfunction(v) && iscfunction(v);
}

/* }}} */

bool lua_equal(LVM *self, int offset1, int offset2) {
    const TValue *lhs = lua_poke(self, offset1);
    const TValue *rhs = lua_poke(self, offset2);
    if (lhs->type != rhs->type) {
        return false;
    }
    switch (lhs->type) {
    case LUA_TBOOLEAN:  return lhs->as.boolean == rhs->as.boolean;
    case LUA_TNIL:      return true; // nil == nil, always.
    case LUA_TNUMBER:   return lhs->as.number == rhs->as.number;
    case LUA_TTABLE:    // All objects are interned so pointer comparisons work.
    case LUA_TFUNCTION:
    case LUA_TSTRING:   return lhs->as.object == rhs->as.object;
    default:            return false; // LUA_TNONE and LUA_TCOUNT
    }
}

/* 'AS' FUNCTIONS ------------------------------------------------------- {{{ */

bool lua_asboolean(LVM *self, int offset) {
    const TValue *v = lua_poke(self, offset);
    return !isfalsy(v);
}

lua_Number lua_asnumber(LVM *self, int offset) {
    const TValue *v = lua_poke(self, offset); 
    return isnumber(v) ? asnumber(v) : (lua_Number)0;
}

TString *lua_aststring(LVM *self, int offset) {
    TValue *v = lua_poke(self, offset);
    return (isstring(v)) ? asstring(v) : NULL;
}

TFunction *lua_asfunction(LVM *self, int offset) {
    TValue *v = lua_poke(self, offset);
    return (isfunction(v)) ? asfunction(v) : NULL;
}

/* }}} */

/* PUSH FUNCTIONS ------------------------------------------------------- {{{ */

void lua_pushobject(LVM *self, const TValue *object) {
    *self->sp = *object;
    self->sp++;
}

void lua_pushboolean(LVM *self, bool b) {
    TValue v = makeboolean(b);
    lua_pushobject(self, &v);
}

void lua_pushnil(LVM *self) {
    lua_pushobject(self, &nilobject);
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

void lua_pushfunction(LVM *self, TFunction *tfunc) {
    TValue v = makefunction(tfunc);
    lua_pushobject(self, &v);
}

void lua_pushcfunction(LVM *self, lua_CFunction function) {
    TFunction *tfunc = new_cfunction(self, function);
    lua_pushfunction(self, tfunc);
}

/* }}} */

void lua_concat(LVM *self) {
    TString *rhs = lua_aststring(self, -1);
    TString *lhs = lua_aststring(self, -2);
    lua_popn(self, 2); // Clean up operands

    size_t len = lhs->len + rhs->len;
    char *data = allocate(char, len + 1);

    memcpy(&data[0],        lhs->data, lhs->len);
    memcpy(&data[lhs->len], rhs->data, rhs->len);
    data[len] = '\0';
    lua_pushlstring(self, data, len);
}

void lua_error(LVM *self, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    for (int i = self->fc - 1; i >= 0; i--) {
        const LFunction *function = self->frames[i].function;
        const Chunk *chunk        = &function->chunk;
        fprintf(stderr, "%s:%i: in ", self->name, current_line(chunk));
        if (function->name == NULL) {
            fprintf(stderr, "main chunk\n");
        } else {
            fprintf(stderr, "function '%s'\n", function->name->data);
        }
    }
    longjmp(self->errjmp, 1);
}
