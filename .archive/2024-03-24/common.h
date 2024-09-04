#ifndef LUA_COMMON_H
#define LUA_COMMON_H

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
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
LUA_OPSIZE_BYTE2:  2-byte operand, e.g. operand to `OP_JMP`.
LUA_OPSIZE_BYTE3:  3-byte operand, e.g. operand to `OP_LCONSTANT`. */

#define LUA_OPSIZE_NONE         (0)
#define LUA_OPSIZE_BYTE         (1)
#define LUA_OPSIZE_BYTE2        (2)
#define LUA_OPSIZE_BYTE3        (3)

/* }}} ---------------------------------------------------------------------- */

/* CONVENIENCE MACROS --------------------------------------------------- {{{ */

/**
 * @param value     Some unsigned integer value to be bit masked.
 * @param offset    How many byte-groups away from the least-significant byte
 *                  the desired byte mask is. e.g. in `0b11010011_01101101`
 *                  group 0 is `01101101`, and group 1 is `11010011`.
 */
#define bytemask(n, offset)     (((n) >> bytetobits(offset)) & LUA_MAXBYTE)
#define byteunmask(n, offset)   ((n) << bytetobits(offset))
#define bytetobits(n)           ((n) * CHAR_BIT)
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

/* Inclusive range. */
#define incrange(n, start, end) ((n) >= start && (n) <= end)

/* Exclusive range. */
#define excrange(n, start, end) ((n) >= start && (n) < end)

/* Help avoid warnings with unused variables/parameters. */
#define unused(x)               (void)(x)
#define unused2(x, y)           unused(x);     unused(y)
#define unused3(x, y, z)        unused2(x, y); unused(z)

/* }}} ---------------------------------------------------------------------- */

/**
 * III:19.2     Struct Inheritance
 *
 * Forward declared in `value.h` so that we can avoid circular dependencies
 * between it and `object.h` as they both require each other's typedefs.
 *
 * This represents a generic heap-allocated Lua datatype: strings, tables, etc.
 */
typedef struct Object Object;

/* Tagged union for Lua's fundamental datatypes. */
typedef struct TValue TValue;

/**
 * III:19.2     Struct Inheritance
 *
 * The `TString` datatype contains an array of characters and a count. Most
 * importantly, its first structure member is an `Object`.
 *
 * This allows standards-compliant type-punning, e.g given `TString*`, we can
 * safely cast it to `Object*` and access the Object fields just fine.
 *
 * Likewise, if we are ABSOLUTELY certain a particular `Object*` points to a
 * `TString*`, then the inverse works was well.
 */
typedef struct TString TString;

/**
 * For now, our Lua table is just a pure hashtable with no array portion. This
 * is inefficient but it does make the implementation a little bit simpler.
 */
typedef struct Table Table;

typedef struct Proto Proto;

/**
 * III:24.1     Function Objects
 *
 * Both Lox and Lua support functions as "first class objects". What this means
 * is that functions can be assigned to variables and created at runtime. This
 * also means that they can be garbage collected, hence just like `TString` the
 * very first member MUST be an `Object`.
 *
 * For our sake we consider each and every function as having its own `Chunk` so
 * that we don't have to manage one monolothic chunk in the Compiler/VM.
 *
 * III:24.7     Native Functions
 *
 * We now need to differentiate between functions created directly from Lua
 * and functions created directly from C, hence the 'L' prefix.
 */
typedef struct TClosure TClosure;

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
typedef struct LVM LVM;

#endif /* LUA_COMMON_H */
