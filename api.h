#ifndef LUA_API_H
#define LUA_API_H

#include "common.h"
#include "value.h"
#include "object.h"

/**
 * To 'register' a C function into Lua, use an array of this type.
 * 
 * See:
 * - https://www.lua.org/source/5.1/lauxlib.h.html#luaL_Reg
 */
typedef struct {
    const char *name;
    lua_CFunction func;
} RegFunc;

/* BASIC STACK MANIPULATION --------------------------------------------- {{{ */

/**
 * III:23.3     While Statements
 *
 * Custom addition to mimick the Lua C API. Get the 0-based index of the current
 * top of the given VM instance's stack. You can also think of this as returning
 * the number of contiguous elements currently written to the stack.
 *
 * NOTE:
 *
 * Assumes that sp is ALWAYS greater than stack base.
 */
size_t lua_gettop(LVM *self);

/**
 * III:23.3     While Statements
 *
 * Set the stack top pointer in relation to a particular `offset`.
 * If the new top is much greater than the original, we fill the gaps with nil
 * values to compensate.
 */
void lua_settop(LVM *self, int offset);

/**
 * III:23.3     While Statements
 *
 * Given a particular offset `n`, if it's negative we get the absolute index in
 * relation to the top of the stack. Overwise we return it as is.
 */
#define lua_absindex(vm, n)     ((n) < 0 ? lua_gettop(vm) + (n) : (n))

/**
 * Convert a positive or negative offset into a pointer to a particular value in
 * the VM's stack. If invalid we return the address of `noneobject` rather than
 * return `NULL` as that'll be terrible.
 *
 * If negative, we use a negative offset relative to the stack top pointer.
 * If positive, we use a positive offset relative to the stack base pointer.
 *
 * See:
 * - https://www.lua.org/source/5.1/lapi.c.html#index2adr
 */
TValue *lua_poke(LVM *self, int offset);

/**
 * Exactly the same as `lua_poke()` except that we immediately dereference the
 * retrieved pointer.
 */
#define lua_peek(vm, n)         (*lua_poke(vm, n))

/* Pop `n` elements from the stack by decrementing the stack top pointer. */
#define lua_popn(vm, n)         lua_settop(vm, -(n)-1)

/* }}} ---------------------------------------------------------------------- */

/* TYPE HELPERS --------------------------------------------------------- {{{ */

/* 'IS' FUNCTIONS ------------------------------------------------------- {{{ */

#define lua_isboolean(vm, n)    (lua_type(vm, n) == LUA_TBOOLEAN)
#define lua_isfunction(vm, n)   (lua_type(vm, n) == LUA_TFUNCTION)
#define lua_isnil(vm, n)        (lua_type(vm, n) == LUA_TNIL)
#define lua_isnumber(vm, n)     (lua_type(vm, n) == LUA_TNUMBER)
#define lua_isstring(vm, n)     (lua_type(vm, n) == LUA_TSTRING)

bool lua_iscfunction(LVM *self, int offset);

/* }}} ---------------------------------------------------------------------- */

/* 'AS' FUNCTIONS ------------------------------------------------------- {{{ */

bool lua_asboolean(LVM *self, int offset);
lua_Number lua_asnumber(LVM *self, int offset);
TString *lua_aststring(LVM *self, int offset);
TFunction *lua_asfunction(LVM *self, int offset);

/* }}} ---------------------------------------------------------------------- */

/* 'TO' FUNCTIONS ------------------------------------------------------- {{{ */

/* }}} ---------------------------------------------------------------------- */

/* 'PUSH' FUNCTIONS ----------------------------------------------------- {{{ */

/**
 * Simply copies `object` by value to the current top of the stack as pointed
 * to by `self->sp`. Afterwards, `self->sp` is incremented to point to the
 * next free slot in the stack.
 */
void lua_pushobject(LVM *self, const TValue *object);
void lua_pushboolean(LVM *self, bool b);
void lua_pushnil(LVM *self);
void lua_pushnumber(LVM *self, lua_Number n);

/**
 * Assumes that `data` is either NULL or a heap-allocated and nul-terminated
 * string buffer with which we'll attempt to take ownership of.
 */
void lua_pushstring(LVM *self, char *data);

/**
 * Assumes that `data` is a heap-allocated buffer which we'll attempt to take
 * ownership of via a call to `take_string()`.
 */
void lua_pushlstring(LVM *self, char *data, size_t len);

/**
 * Assumes `data` is a read-only nul-terminated C string literal with which we
 * try to create a `TString*` of. Do NOT use this with malloc'd strings.
 */
void lua_pushliteral(LVM *self, const char *data);
void lua_pushfunction(LVM *self, TFunction *luafn);
void lua_pushcfunction(LVM *self, lua_CFunction func);

/* }}} ---------------------------------------------------------------------- */

/* }}} ---------------------------------------------------------------------- */

/**
 * Poke at the value at the given positive or negative offset into the VM stack.
 * We return whatever its current type is. As is, we do not do much error checks
 * so this is quite fragile if you provide an invalid index or offset.
 */
VType lua_type(LVM *self, int offset);
const char *lua_typename(LVM *self, VType type);
bool lua_equal(LVM *self, int offset1, int offset2);

/**
 * III:19.4.1   Concatenation
 *
 * String concatenation is quite tricky due to all the allocations we need to
 * make! Not only that, but multiple concatenations may end up "orphaning"
 * middle strings and thus leaking memory.
 */
void lua_concat(LVM *self);

/**
 * III:18.3.1   Unary negation and runtime errors
 *
 * This function simply prints whatever formatted error message you want.
 *
 * III:24.5.3   Printing stack traces
 *
 * We've now added stack traces to help users identify where their program may
 * have gone wrong. It includes a dump of the call stack up until that point.
 */
void lua_error(LVM *self, const char *format, ...);

#endif /* LUA_API_H */
