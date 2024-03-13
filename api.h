#ifndef LUA_API_H
#define LUA_API_H

#include "common.h"
#include "value.h"
#include "object.h"

typedef enum {
    LUA_ERROR_ARITH,
    LUA_ERROR_COMPARE,
    LUA_ERROR_CONCAT,
} ErrType;

/**
 * To 'register' a C function into Lua, use an array of this type. This array
 * MUST have a sentinel value at the very end containing `NULL` for both
 * members to indicate the end of the array, as we don't determine the count.
 * 
 * See:
 * - https://www.lua.org/source/5.1/lauxlib.h.html#luaL_Reg
 */
typedef struct {
    const char *name;
    lua_CFunction func;
} RegFn;

typedef RegFn lua_Library[];

/**
 * III:24.7     Native Functions
 *
 * When garbage collection gets involved, it will be important to consider if
 * during the call to `copy_string()` and `new_function` if garbage collection
 * was triggered. If that happens we must tell the GC that we are not actually
 * done with this memory, so storing them on the stack (will) accomplish that
 * when we get to that point.
 * 
 * NOTE:
 * 
 * Since this is called during VM initialization, we can safely assume that the
 * stack is currently empty.
 */
void lua_registerlib(LVM *self, const lua_Library library);

/* BASIC STACK MANIPULATION --------------------------------------------- {{{ */

/**
 * Read the current instruction and move the instruction pointer.
 *
 * Remember that postfix increment returns the original value of the expression.
 * So we effectively increment the pointer but we dereference the original one.
 */
#define lua_nextbyte(vm)        (*(vm)->cf->ip++)

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
 * Assumes that the current function's instruction pointer points to a 2-byte
 * operand for an `OP_JMP` instruction of some kind.
 * 
 * NOTE:
 * 
 * `OP_LOOP` jumps backwards, so do subtract from the instruction pointer pass
 * in `-1` for `sign`.
 */
void lua_dojmp(LVM *self);
void lua_dofjmp(LVM *self);
void lua_doloop(LVM *self);

/**
 * Assumes that the instruction pointer currently points to a 1-byte operand
 * representing the number of comma-separated arguments that the compiler was
 * able to resolve between the parentheses. This function can call either a Lua
 * or a C function, the object of which should be located below the stack pointer
 * at the offset `sp - 1 - argc` since it was pushed first.
 */
bool lua_call(LVM *self, int argc);

/**
 * If we finished the top-level chunk, returns `true`.
 */
bool lua_return(LVM *self);

/* 'GET' AND 'SET' FUNCTIONS ----------------------------------------------- {{{ 
 The following functions assume that the current function's instruction
 pointer points to a 1 or 3 byte operand to a particular instruction. This byte
 or bytes will represent an absolute index into the current functions' constant
 values array. This array was populated by the compiler and contains, in order,
 the identifiers and literals we need. What we do with this constant is up to
 the particular instruction. */

void lua_getglobal(LVM *self);
void lua_getlglobal(LVM *self);
void lua_getlocal(LVM *self);

void lua_setglobal(LVM *self);
void lua_setlglobal(LVM *self);
void lua_setlocal(LVM *self);

/* }}} ---------------------------------------------------------------------- */

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
#define lua_pop(vm, n)         lua_settop(vm, -(n)-1)

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

/* 'PUSH' FUNCTIONS ----------------------------------------------------- {{{ */

/**
 * Assumes that the current CallFrame's instruction pointer is currently at the
 * 1-byte operand for an `OP_CONSTANT` instruction which we will use to index
 * into the CallFrame's functions' chunk's constants array.
 */
void lua_pushconstant(LVM *self);

/**
 * Similar to `lua_pushconstant` except that we instead assume a 3-byte operand.
 * See the comments for that function for more information.
 */
void lua_pushlconstant(LVM *self);

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

/**
 * Push the given tagged union function object `tfunc` to the top of the stack.
 * This is mainly used so it can be immediately followed by an `OP_CALL` opcode.
 */
void lua_pushfunction(LVM *self, TFunction *tfunc);
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
void lua_unoperror(LVM *self, int n, ErrType err);
void lua_binoperror(LVM *self, int n1, int n2, ErrType err);

#endif /* LUA_API_H */
