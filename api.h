#ifndef LUA_API_H
#define LUA_API_H

#include "common.h"
#include "conf.h"
#include "value.h"

/* LUA STACK MANIPULATION ----------------------------------------------- {{{ */

/**
 * Get an index into the VM's stack representing the first free slot, a.k.a.
 * 1 past the last written element in the stack.
 */
int lua_gettop(lua_VM *self);

/**
 * Sets the VM's stack pointer to point to itself plus some offset.
 * 
 * 1. `index >= 0`: Until `sp == bp + index`, set all intermediate values to nil.
 *                  We then set sp to be `bp + index`.
 * 2. `index < 0`:  We subtract `index` and add 1 to sp. Since the stack is
 *                  stack-allocated, no cleanup is done.
 *                  
 * Returns the value right before the first "free" element in the stack.
 */
TValue lua_settop(lua_VM *self, int index);

/**
 * Pop `n` elements from the VM's stack values array. We use negative indexes
 * to indicate that these are negative offsets in relation to sp.
 * 
 * We return the value located at the stack pointer after the decrement, sub. 1
 * because it points to the next free slot. So we want the last occupied slot.
 */
#define lua_popvalues(vm, n) lua_settop(vm, -(n)-1)

/**
 * Wrapper around `lua_popvalues` that only ever pops 1 value and returns the
 * new top of the stack - 1.
 */
#define lua_popvalue(vm)     lua_popvalues(vm, 1)

void lua_setobj(lua_VM *self, TValue *dst, const TValue *src);

/**
 * Get a pointer to the VM's stack array given a negative offset in relation to
 * the current value of the stack pointer.
 * 
 * Does not modify any of the VM's states.
 * 
 * To poke at current top of the stack, use `0`. To poke at 1 slot below that,
 * use `1`, so on and so forth.
 */
TValue *lua_pokevalue(lua_VM *self, int offset);

/**
 * Check what value lies at the given negative offset in relation to the current
 * value of the stack pointer.
 * 
 * Does not modify any of the VM's state.
 * 
 * The peek the latest written value on the stack, use `0`. To peek at 1 slot
 * below that, use `1`. So on and so forth.
 */
TValue lua_peekvalue(lua_VM *self, int offset);

/* }}} */

/* LUA STACK READ FUNCTIONS --------------------------------------------- {{{ */

/**
 * Read the instruction pointer, assuming it's a simple byte instruction.
 * It could be an opcode or an operand to an opcode.
 */
Byte lua_readbyte(lua_VM *self);

/**
 * Read the instruction pointer 3 times to get 3 byte instructions.
 * We then combine them to create a 24-bit integer which is our index into the
 * constants array for example. Used by `OP_CONSTANT_LONG` and similar.
 */
DWord lua_readdword(lua_VM *self);

/**
 * Read the constant at the index given by the instruction pointer, incr. ip
 * We use this index to retrieve a value in the VM's constants pool then copy 
 * that to the top of the stack, then incr. sp.
 */
void lua_pushconstant(lua_VM *self);

TValue lua_readconstant(lua_VM *self);

/**
 * Interpret the next 3 instructions as representing a 24-bit operand which we
 * will combine to create an index into the constants bool.
 */
TValue lua_readconstant_long(lua_VM *self);

/**
 * Interpret the current instruction pointer as containing an index into the
 * constants pool. This index is assumed to contain a `lua_String*`.
 */
lua_String *lua_readstring(lua_VM *self);

/**
 * Interpret the next 3 instructions as representing a 24-bit operand.
 * This index is assumed to contain a `lua_String*` with no type validation.
 */
lua_String *lua_readstring_long(lua_VM *self);

/* }}} */

/* LUA STACK PUSH FUNCTIONS (C->STACK) ---------------------------------- {{{ */

/**
 * Copy value at the given VM's stack index into the VM's top of the stack.
 * Afterwards we increment the stack pointer.
 * 
 * See: https://www.lua.org/source/5.1/lapi.c.html#lua_pushvalue
 */
// void lua_pushvalue(lua_VM *self, int index);

void lua_pushboolean(lua_VM *self, bool b);

/**
 * This is where my implementation significantly differs from the official Lua
 * one. It seems they directly emit their constants in the stack array?
 * 
 * Whereas the Lox-like implementation contains a separate constants array. Se
 * we need to account for it in our implementation.
 * 
 * This function sets the top of the stack to the constant found at the given
 * index into the constants pool.
 */
void lua_pushconstant_long(lua_VM *self);
void lua_pushnil(lua_VM *self);
void lua_pushnumber(lua_VM *self, lua_Number n);

/**
 * Unlike the official Lua implementation, we don't take an index into the VM's
 * stack where the value is located. Instead we pass a raw `TValue` to be copied
 * into the top of the stack.
 */
void lua_pushvalue(lua_VM *self, TValue value);

/* }}} */

/**
 * Returns type of the value in the VM's stack array at the given index/offset.
 */
ValueType lua_type(lua_VM *self, int index);

/**
 * Returns a string literal of the given type enum, usually a result of `lua_type`.
 */
const char *lua_typename(lua_VM *self, int type);

/**
 * Later on, when we support varargs, the number of arguments will be at the top
 * of the stack first and then we'll have to loop.
 */
void lua_print(lua_VM *self);

#endif /* LUA_API_H */
