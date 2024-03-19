#ifndef LUA_API_H
#define LUA_API_H

#include "common.h"
#include "value.h"
#include "object.h"

#define LUA_GLOBALSINDEX    (-10002)

typedef enum {
    LUA_ERROR_ARITH,
    LUA_ERROR_COMPARE,
    LUA_ERROR_CONCAT,
    LUA_ERROR_INDEX,   // Using '[]' on non-tables
    LUA_ERROR_FIELD,   // Got non-string as key when a string was expected.
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
} lua_RegisterFn;

typedef lua_RegisterFn lua_Library[];

/**
 * III:24.7     Native Functions
 *
 * When garbage collection gets involved, it will be important to consider if
 * during the call to `copy_string()` and `new_function` if garbage collection
 * was triggered. If that happens we must tell the GC that we are not actually
 * done with this memory, so storing them on the stack (will) accomplish that
 * when we get to that point.
 * 
 * @param self      A Lua VM instance to manage.
 * @param name      Desired module table name, assumes it already exists.
 * @param library   Array of `lua_RegisterFn` instances to register.
 */
void lua_loadlibrary(LVM *self, const char *name, const lua_Library library);

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
 * Assumes that sp is ALWAYS greater than or equal to the current base pointer.
 * 
 * III:24.7     Native Functions
 * 
 * Each function has its own "window" into the stack frame, where their base 
 * pointer points to the calling function object itself rather than the very 
 * bottom of the stack.
 */
int lua_gettop(LVM *self);

/**
 * III:23.3     While Statements
 *
 * Set the stack top pointer in relation to a particular `offset`.
 * If the new top is much greater than the original, we fill the gaps with nil
 * values to compensate.
 */
void lua_settop(LVM *self, int offset);

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

void lua_getfield(LVM *self, int offset, const char *field);

/**
 * Simply pushes the global value associated with identifier `s` to the stack.
 * It is your responsibility to pop this value off as needed.
 * 
 * @param vm    LVM*
 * @param s     char*
 */
#define lua_getglobal(vm, s)    lua_getfield(vm, LUA_GLOBALSINDEX, s)


/**
 * Does the equivalent of `tbl[key] = val`, where:
 * 1. `tbl` is at the given offset from the stack.
 * 2. `key` is just below the value at the top of the stack.
 * 3. `val` is the value at the top of the stack.
 *
 * This function pops the key and the value from the stack, but not the table.
 *
 * See:
 * - https://www.lua.org/manual/5.1/manual.html#lua_settable
 */
void lua_settable(LVM *self, int offset);

/**
 * Assumes that a value to be stored in a table is at the top of the stack.
 *
 * @param offset    Index/offset table in current stack frame window. If using a
 *                  pseudo-index like `LUA_GLOBALSINDEX`, retrieve the pointer
 *                  to the value associated with that pseudo-index rather than 
 *                  an actual value from the stack.
 * @param field     Desired field name.
 * 
 * This function pops the value from the stack. See:
 * - https://www.lua.org/manual/5.1/manual.html#lua_setfield
 */
void lua_setfield(LVM *self, int offset, const char *field);

/**
 * Assigns the value at the top of the stack to the global variable associated
 * with the Lua-facing identifier `s`.
 * 
 * @param vm    LVM*
 * @param s     char*
 */
#define lua_setglobal(vm, s)    lua_setfield(vm, LUA_GLOBALSINDEX, s)

/* }}} ---------------------------------------------------------------------- */

/**
 * III:23.3     While Statements
 *
 * Given a particular offset `n`, if it's negative we get the absolute index in
 * relation to the top of the stack. Overwise we return it as is.
 */
#define lua_absindex(vm, n)     ((n) < 0 ? lua_gettop(vm) + (n) : (n))

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
#define lua_istable(vm, n)      (lua_type(vm, n) == LUA_TTABLE)

bool lua_iscfunction(LVM *self, int offset);

/* }}} ---------------------------------------------------------------------- */

/* 'AS' FUNCTIONS ------------------------------------------------------- {{{ */

bool lua_asboolean(LVM *self, int offset);
lua_Number lua_asnumber(LVM *self, int offset);
TString *lua_aststring(LVM *self, int offset);
TFunction *lua_asfunction(LVM *self, int offset);
Table *lua_astable(LVM *self, int offset);

/* }}} ---------------------------------------------------------------------- */

/* 'TO' FUNCTIONS ------------------------------------------------------- {{{ */

/**
 * III:24.7     Native Functions
 * 
 * Create the string representation of the `TValue` at the given `offset` into
 * the VM's stack. May return a pointer to a string literal or to a `TString*`
 * buffer.
 * 
 * NOTE:
 * 
 * Although this function may allocate particular strings (numbers/pointers), 
 * you are not meant to free any strings returned from this. Also, this function
 * has literally zero error handling as the idea is ANY value can be represented
 * as a string. 
 * 
 * The only limits are the current machine's number of digits in a virtual 
 * memory address (e.g. `0x55ed37299a40`), or, for `lua_Number`, the configured 
 * desired maximum precision, e.g. `"%.14g"`.
 */
const char *lua_tostring(LVM *self, int offset);

/**
 * Converts the `TValue` at given offset into the VM's stack to a `lua_Number`.
 * If it's a number to begin with, we return the number as-is.
 * If it's a string, we attempt to convert it to a numerical representation.
 * Otherwise, we return 0 for any other type.
 * 
 * NOTE:
 * 
 * This function does not signal any errors by itself. You may want to pair it
 * with calls to `lua_isnumber` and `lua_isstring`.
 */
lua_Number lua_tonumber(LVM *self, int offset);

/* }}} */

/* 'PUSH' FUNCTIONS ----------------------------------------------------- {{{ */

/**
 * Simply copies `object` by value to the current top of the stack as pointed
 * to by `self->sp`. Afterwards, `self->sp` is incremented to point to the
 * next free slot in the stack.
 */
#define lua_pushobject(vm, o)   (*(vm)->sp++ = *(o))

void lua_pushboolean(LVM *self, bool b);
void lua_pushnil(LVM *self);
void lua_pushnumber(LVM *self, lua_Number n);

void lua_pushstring(LVM *self, const char *data);
void lua_pushlstring(LVM *self, const char *data, size_t len);

#define lua_pushliteral(vm, s)  lua_pushlstring(vm, s, arraylen(s) - 1)

void lua_pushtable(LVM *self, Table *table);

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
 * Utility function to print the contents of the stack at the time of calling.
 * Prints vertically from top of the stack going to the bottom.
 * It also identifies which of the stack values are the stack/base pointer.
 */
void lua_dumpstack(LVM *self);

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

#define lua_badarg(info) "Bad argument #%i to '%s' (" info ")"
#define lua_argany(vm, argn, name) \
    lua_error(vm, lua_badarg("value expected"), argn, name)
#define lua_typerror(vm, argn, name, want, got) \
    lua_error(vm, lua_badarg("%s expected, got %s"), argn, name, want, got)

#endif /* LUA_API_H */
