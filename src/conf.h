/**
 * @brief   14.2: Getting Started
 */
#ifndef LULU_CONFIGURATION_H
#define LULU_CONFIGURATION_H

#include <limits.h>
#include <math.h>           /* Needed by `num_*` macros. */
#include <stdbool.h>        /* C99 `_Bool` along with alias `bool` */
#include <stddef.h>         /* `size_t`, `NULL` */
#include <stdlib.h>         /* `realloc`, `free` */
#include <stdio.h>          /* `printf` family, `f*` family (`FILE*`) */
#include <stdint.h>
#include <setjmp.h>         /* `jmp_buf`, `setjmp`, `longjmp` */
#include <string.h>         /* `str*` family, `mem*` family */

// Must be large enough to hold all `OpCode` enumerations as well as act as part
// of each instruction's operand. This MUST be unsigned.
typedef uint8_t     Byte;
typedef uint16_t    Byte2;
typedef uint32_t    Byte3; // We only need 24 bits at most but this will do.

#define PROMPT      "> "
#define MAX_STACK   0x100
#define MAX_LINE    0x100

/* --- NUMBER TYPE INFORMATION -------------------------------------------- {{{1
You may wish to change `MAX_NUMTOSTRING` based on the following conditions:
1.  `NUMBER_FMT` uses a precision larger 64 digits or is longer than the format
    specification of #2.
2.  The formatted length of `"function: %p", (void*)p"` is greater than or equal
    to 64 characters.
*/

#define NUMBER_TYPE        double
#define NUMBER_SCAN        "%lf"
#define NUMBER_FMT         "%.14g"
#define MAX_NUMTOSTRING     64
#define num_tostring(s, n)  snprintf((s), MAX_NUMTOSTRING, NUMBER_FMT, (n))
#define num_add(a, b)       ((a) + (b))
#define num_sub(a, b)       ((a) - (b))
#define num_mul(a, b)       ((a) * (b))
#define num_div(a, b)       ((a) / (b))
#define num_mod(a, b)       (fmod(a, b))
#define num_pow(a, b)       (pow(a, b))
#define num_unm(a)          (-(a))
#define num_eq(a, b)        ((a) == (b))
#define num_lt(a, b)        ((a) <  (b))
#define num_le(a, b)        ((a) <= (b))
#define num_isnan(a)        (!num_eq(a, b))

// See `man 3 strtod`. For other `cstr_*` macros, see `limits.h`.
#define cstr_tonumber(s, p) strtod(s, p)
#define nil_tostring(s)     snprintf((s), MAX_NUMTOSTRING, "nil")
#define bool_tostring(s, b) snprintf((s), MAX_NUMTOSTRING, (b) ? "true" : "false")
#define ptr_tostring(s, p)  snprintf((s), MAX_NUMTOSTRING, "%p", (p))

// 1}}} ------------------------------------------------------------------------

#endif /* LULU_CONFIGURATION_H */
