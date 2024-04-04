/**
 * @brief   14.2: Getting Started
 */
#ifndef LULU_CONFIGURATION_H
#define LULU_CONFIGURATION_H

#include <assert.h>         /* `assert`, `static_assert` */
#include <limits.h>
#include <math.h>           /* Needed by `number_*` macros. */
#include <stdbool.h>        /* C99 `_Bool` along with alias `bool` */
#include <stddef.h>         /* `size_t`, `NULL` */
#include <stdlib.h>         /* `realloc`, `free` */
#include <stdio.h>          /* `printf` family, `f*` family (`FILE*`) */
#include <stdint.h>
#include <string.h>         /* `str*` family, `mem*` family */

// Must be large enough to hold all `OpCode` enumerations as well as act as part
// of each instruction's operand. This MUST be unsigned.
typedef uint8_t     Byte;
typedef uint16_t    Byte2;
typedef uint32_t    Byte3; // We only need 24 bits at most but this will do.

// --- HELPER MACROS ------------------------------------------------------ {{{1

#define _stringify(x)       #x
#define stringify(x)        _stringify(x)
#define loginfo()           __FILE__ ":" stringify(__LINE__)
#define logformat(s)        loginfo() ": " s
#define logprintln(s)       fputs(logformat(s) "\n", stderr)
#define logprintf(s, ...)   fprintf(stderr, logformat(s), __VA_ARGS__)
#define logprintfln(s, ...) fprintf(stderr, logformat(s) "\n", __VA_ARGS__)

/* Will not work for pointer-decayed arrays. */
#define arraylen(array)     (sizeof(array) / sizeof(array[0]))
#define arraysize(T, N)     (sizeof(T) * (N))

/* Get the number of bits that `N` bytes holds. */
#define bytes_to_bits(N)    ((N) * CHAR_BIT)
#define bitsize(T)          bytes_to_bits(sizeof(T))

#define cast(T, expr)       (T)(expr)
#define unused(x)           (void)(x)
#define unused2(x, y)       unused(x); unused(y) 

#define MAX_BYTE            cast(Byte,  -1)
#define MAX_BYTE2           cast(Byte2, -1)
#define MAX_BYTE3           ((1 << bytes_to_bits(3)) - 1)

// 1}}} ------------------------------------------------------------------------

/* --- NUMBER TYPE INFORMATION -------------------------------------------- {{{1
You may wish to change `MAX_NUMBER2STRING` based on the following conditions:
1.  `NUMBER_FMT` uses a precision larger 64 digits or is longer than the format
    specification of #2.
2.  The formatted length of `"function: %p", (void*)p"` is greater than or equal
    to 64 characters.
*/

#define NUMBER_TYPE         double
#define NUMBER_SCAN         "%lf"
#define NUMBER_FMT          "%.14g"
#define MAX_NUMBER2STRING   64
#define number_tostring(s, n) snprintf((s), MAX_NUMBER2STRING, NUMBER_FMT, (n))
#define string_tonumber(s, p) strtod(s, p)
#define number_add(a, b)    ((a) + (b))
#define number_sub(a, b)    ((a) - (b))
#define number_mul(a, b)    ((a) * (b))
#define number_div(a, b)    ((a) / (b))
#define number_mod(a, b)    (fmod(a, b))
#define number_pow(a, b)    (pow(a, b))
#define number_unm(a)       (-(a))

// 1}}} ------------------------------------------------------------------------

#endif /* LULU_CONFIGURATION_H */
