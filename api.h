#ifndef LUA_API_H
#define LUA_API_H

#include "common.h"
#include "value.h"
#include "object.h"

/**
 * III:23.3     While Statements
 * 
 * Custom addition to mimick the Lua C API. Get the 0-based index of the current
 * top of the given VM instance's stack. You can also think of this as returning
 * the number of contiguous elements currently written to the stack.
 */
#define lua_gettop(vm)          ((vm)->sp - (vm)->bp)

/**
 * III:23.3     While Statements
 * 
 * Given a particular offset `n`, if it's negative we get the absolute index in
 * relation to the top of the stack. Overwise we return it as is.
 */
#define lua_absindex(vm, n)     ((n) < 0 ? lua_gettop(vm) + (n) : (n))

/**
 * III:23.3     While Statements
 * 
 * Given a particular offset `n`, get a pointer to the value in the stack.
 * If negative, we use a negative offset relative to the stack top pointer.
 * If positive, we use a positive offset relative to the stack base pointer.
 */
#define lua_ptroffset(vm, n)    ((n) < 0 ? (vm)->sp + (n) : (vm)->bp + (n))

/* Pop `n` elements from the stack by decrementing the stack top pointer. */
#define lua_pop(vm, n)          lua_settop(vm, -(n)-1)
#define lua_isnil(vm, i)        lua_istype(vm, i, LUA_TNIL)
#define lua_isboolean(vm, i)    lua_istype(vm, i, LUA_TBOOLEAN)
#define lua_isnumber(vm, i)     lua_istype(vm, i, LUA_TNUMBER)
#define lua_isstring(vm, i)     lua_istype(vm, i, LUA_TSTRING)

/** 
 * Due to the '.' character we can't use '##'. This is because `as.##tag` would
 * result in one token for `.##tag` which is not a valid construct. But due to
 * preprocessor pasting rules, macros are substituted even before any member
 * accesses.
 * 
 * See:
 * - https://stackoverflow.com/a/52092333
 */
#define lua_astype(vm, i, tag)  ((vm)->stack[i].as.tag)
#define lua_asboolean(vm, i)    lua_astype(vm, i, boolean)
#define lua_asnumber(vm, i)     lua_astype(vm, i, number)
#define lua_asobject(vm, i)     lua_astype(vm, i, object)
#define lua_asstring(vm, i)     (lua_String*)lua_asobject(vm, i)

/* FUNCTION PROTOTYPES -------------------------------------------------- {{{ */

/**
 * III:23.3     While Statements
 * 
 * Set the stack top pointer in relation to a particular `offset`.
 * If the new top is much greater than the original, we fill the gaps with nil
 * values to compensate.
 */
void lua_settop(lua_VM *self, ptrdiff_t offset);

/**
 * III:23.3     While Statements
 * 
 * Query the value at the given positive or negative offset into the VM stack
 * if it matches the particular given type.
 */
bool lua_istype(const lua_VM *self, ptrdiff_t offset, ValueType type);

/* }}} */

#endif /* LUA_API_H */
