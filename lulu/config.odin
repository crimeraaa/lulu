#+private
package lulu

import "core:math"

// Minimum stack size guaranteed to be available to all functions
STACK_MIN :: 8

// Compile-time string constants
MEMORY_ERROR_STRING :: "Out of memory"

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

// Numbers
NUMBER_TYPE :: f64
NUMBER_FMT  :: "%.14g"

number_add :: #force_inline proc "contextless" (a, b: Number) -> Number {
    return a + b
}

number_sub :: #force_inline proc "contextless"  (a, b: Number) -> Number {
    return a - b
}

number_mul :: #force_inline proc "contextless" (a, b: Number) -> Number {
    return a * b
}

number_div :: #force_inline proc "contextless" (a, b: Number) -> Number {
    return a / b
}

/*
Links:
-   https://www.lua.org/source/5.1/luaconf.h.html#luai_nummod
 */
number_mod :: #force_inline proc "contextless" (a, b: Number) -> Number {
    return a - math.floor(a / b)*b
}

number_pow :: #force_inline proc "contextless" (a, b: Number) -> Number {
    return math.pow(a, b)
}

number_eq :: #force_inline proc "contextless" (a, b: Number) -> bool {
    return a == b
}

number_lt :: #force_inline proc "contextless" (a, b: Number) -> bool {
    return a < b
}

number_leq :: #force_inline proc "contextless" (a, b: Number) -> bool {
    return a <= b
}

number_unm :: #force_inline proc "contextless" (a: Number) -> Number {
    return -a
}

number_is_nan :: math.is_nan_f64
