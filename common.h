#ifndef LUA_COMMON_H
#define LUA_COMMON_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define xtostring(Macro)        #Macro
#define stringify(Macro)        xtostring(Macro)
#define logstring(Message)      __FILE__ ":" stringify(__LINE__) ": " Message
#define logprintln(Message)     fputs(logstring(Message) "\n", stderr)
#define logprintf(Format, ...)  fprintf(stderr, logstring(Format), __VA_ARGS__)

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
 * The `lua_String` datatype contains an array of characters and a count. But most
 * importantly, its first structure member is an `lua_Object`.
 * 
 * This allows standards-compliant type-punning, e.g given `lua_String*`, we can
 * safely cast it to `lua_Object*` and access the lua_Object fields just fine. 
 * 
 * Likewise, if we are ABSOLUTELY certain a particular `lua_Object*` points to a 
 * `lua_String*`, then the inverse works was well.
 * 
 * III:19.1     Flexible Array Members (CHALLENGE)
 * 
 * Instead of using a separate pointer for `lua_String*` itself and another one
 * for the `char*` buffer thereof, we can use C99 flexible array members so that
 * we allocate enough memory for the entire structure along with its buffer and
 * cram everything into one pointer. This requires us to NOT free the buffer
 * itself and to only free the pointer as even FAMs decay to pointers, which is
 * dangerous when passing to the malloc family.
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
