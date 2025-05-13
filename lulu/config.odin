#+private
package lulu

// Minimum stack size guaranteed to be available to all functions
STACK_MIN :: 8

// Compile-time string constants
MEMORY_ERROR_STRING :: "Out of memory"
NUMBER_FMT :: "%.14g"

// Compile-time Features
USE_CONSTANT_FOLDING :: #config(USE_CONSTANT_FOLDING, !ODIN_DEBUG)

/*
**Overview**
-   Controls how many recursive calls the parser can enter before throwing
    an error.
-   This limit is arbitrary; it is only intended to prevent infinite loops.

**Links**
-   https://www.lua.org/source/5.1/luaconf.h.html#LUAI_MAXCCALLS
*/
PARSER_MAX_RECURSE :: 200

// Debug Info
DEBUG_TRACE_EXEC :: #config(DEBUG_TRACE_EXEC, ODIN_DEBUG)
DEBUG_PRINT_CODE :: #config(DEBUG_PRINT_CODE, ODIN_DEBUG)
