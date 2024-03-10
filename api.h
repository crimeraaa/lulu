#ifndef LUA_API_H
#define LUA_API_H

#include "common.h"
#include "value.h"
#include "object.h"

/**
 * Unlike other macros and functions `i` must always be positive or zero because
 * we don't have a pointer to the top of the constants array.
 */
#define lua_getconstant(frame, i)  ((frame)->function->chunk.constants.values[i])

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
size_t lua_gettop(const LVM *self);

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
#define lua_poke(vm, n)         ((n) < 0 ? (vm)->sp + (n) : (vm)->stack + (n))

/**
 * Exactly the same as `lua_poke()` except that we immediately dereference the
 * retrieved pointer.
 */
#define lua_peek(vm, n)         (*lua_poke(vm, n))

/* Pop `n` elements from the stack by decrementing the stack top pointer. */
#define lua_popn(vm, n)         lua_settop(vm, -(n)-1)

/* TYPE HELPER MACROS --------------------------------------------------- {{{ */

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
#define lua_astype(vm, i, tag)  ((vm)->stack[lua_absindex(vm, i)].as.tag)
#define lua_asboolean(vm, i)    lua_astype(vm, i, boolean)
#define lua_asnumber(vm, i)     lua_astype(vm, i, number)
#define lua_asobject(vm, i)     lua_astype(vm, i, object)
#define lua_asstring(vm, i)     (TString*)lua_asobject(vm, i)

/* }}} ---------------------------------------------------------------------- */

/* FUNCTION PROTOTYPES -------------------------------------------------- {{{ */

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
 * Query the value at the given positive or negative offset into the VM stack
 * if it matches the particular given type.
 */
bool lua_istype(LVM *self, int offset, VType tagtype);

/**
 * Poke at the value at the given positive or negative offset into the VM stack.
 * We return whatever its current type is. As is, we do not do much error checks
 * so this is quite fragile if you provide an invalid index or offset.
 */
VType lua_type(LVM *self, int offset);
const char *lua_typename(LVM *self, int offset);
bool lua_equal(LVM *self, int offset1, int offset2);

/**
 * III:18.4.1   Logical not and falsiness
 *
 * In Lua, the only "falsy" types are `nil` and the boolean value `false`.
 * Everything else is `truthy` meaning it is treated as a true condition.
 * Do note that this doesn't mean that `1 == true`, it just means that `if 1`
 * and `if true` do the same thing conceptually.
 *
 * III:23.3     While Statements
 *
 * I'm updating the API to be more like the Lua C API. So we take a VM instance
 * pointer and an offset into it, we determine the falsiness of the value at the
 * given index/offset.
 */
bool lua_isfalsy(LVM *self, int offset);

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

/**
 * III:19.4.1   Concatenation
 *
 * String concatenation is quite tricky due to all the allocations we need to
 * make! Not only that, but multiple concatenations may end up "orphaning"
 * middle strings and thus leaking memory.
 */
void lua_concat(LVM *self);

/* }}} ---------------------------------------------------------------------- */

#endif /* LUA_API_H */
