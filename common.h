#ifndef LUA_COMMON_H
#define LUA_COMMON_H

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "conf.h"

#define LUA_MAXBYTE             ((Byte)-1)
#define LUA_MAXWORD             ((Word)-1)
#define LUA_MAXDWORD            ((DWord)-1)
#define LUA_MAXQWORD            ((QWord)-1)

/* --- LUA OPCODE SIZES ---------------------------------------------------- {{{
 Lua opcode operands can come in multiple sizes.
 
 LUA_OPSIZE_NONE:   No operand so we don't add or subtract anything.
 LUA_OPSIZE_BYTE:   1-byte operand, e.g. operand to `OP_GETLOCAL`.
 LUA_OPSIZE_SHORT:  2-byte operand, e.g. operand to `OP_JMP`.
 LUA_OPSIZE_LONG:   3-byte operand, e.g. operand to `OP_LCONSTANT`. */

#define LUA_OPSIZE_NONE         (0)
#define LUA_OPSIZE_BYTE         (1)
#define LUA_OPSIZE_SHORT        (2)
#define LUA_OPSIZE_LONG         (3)

/* }}} ---------------------------------------------------------------------- */

/**
 * @param value     Some unsigned integer value to be bit masked.
 * @param offset    How many byte-groups away from the least-significant byte
 *                  the desired byte mask is. e.g. in `0b11010011_01101101`
 *                  group 0 is `01101101`, and group 1 is `11010011`.
 */
#define bytemask(value, offset)  (((value) >> ((offset) * CHAR_BIT)) & LUA_MAXBYTE)
#define bitsize(T)              (sizeof(T) * CHAR_BIT)
#define xtostring(macro)        #macro
#define stringify(macro)        xtostring(macro)
#define logstring(info)         __FILE__ ":" stringify(__LINE__) ": " info
#define logprintln(info)        fputs(logstring(info) "\n", stderr)
#define logprintf(fmts, ...)    fprintf(stderr, logstring(fmts), __VA_ARGS__)

/**
 * III:23.2     If Statements
 * 
 * Custom addition to quickly create compound literals for array-types, mainly
 * for use inside of other macros.
 */
#define toarraylit(T, ...)      ((T[]){__VA_ARGS__})

/**
 * III:23.2     If Statements
 * 
 * Custom addition to make working with array literals easier.
 * 
 * NOTE:
 * 
 * This will ONLY work with array literals, e.g. `T[]` not `T*`.
 * Some compilers will warn you of that, but it's not a guarantee.
 * So be careful of array-pointer decay!
 */
#define arraylen(arraylit)      (sizeof(arraylit) / sizeof(arraylit[0]))

/**
 * III:19.2     Struct Inheritance
 * 
 * Forward declared in `value.h` so that we can avoid circular dependencies
 * between it and `object.h` as they both require each other's typedefs.
 * 
 * This represents a generic heap-allocated Lua datatype: strings, tables, etc.
 */
typedef struct lua_Object lua_Object;

/**
 * III:19.2     Struct Inheritance
 * 
 * The `lua_String` datatype contains an array of characters and a count. Most
 * importantly, its first structure member is an `lua_Object`.
 * 
 * This allows standards-compliant type-punning, e.g given `lua_String*`, we can
 * safely cast it to `lua_Object*` and access the lua_Object fields just fine. 
 * 
 * Likewise, if we are ABSOLUTELY certain a particular `lua_Object*` points to a 
 * `lua_String*`, then the inverse works was well.
 */
typedef struct lua_String lua_String;

/**
 * III:19.5     Freeing Objects
 * 
 * In order to "fix" cyclic dependencies between headers, I've opted to move some
 * forward declarations here. This is because opaque pointers are allowed in
 * headers and treated as distinct types. This allows us to not need to include
 * `vm.h` in headers which `vm.h` itself includes.
 * 
 * However, for `.c` files, it's perfectly fine to include `vm.h`.
 */
typedef struct lua_VM lua_VM;

#endif /* LUA_COMMON_H */
