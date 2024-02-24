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

#define xtostring(macro)        #macro
#define stringify(macro)        xtostring(macro)
#define logstring(info)         __FILE__ ":" stringify(__LINE__) ": " info
#define logprintln(info)        fputs(logstring(info) "\n", stderr)
#define logprintf(fmts, ...)    fprintf(stderr, logstring(fmts), __VA_ARGS__)

typedef uint8_t  Byte;  // Smallest addressable size for the Intel 8086.
typedef uint16_t Word;  // Primary addressable size for the Intel 8086.
typedef uint32_t DWord; // "Double word" a.k.a. 2 `Word`s long, usually 32-bits.
typedef uint64_t QWord; // "Quad word" a.k.a 4 `Word`s long, usually 64-bits.

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
