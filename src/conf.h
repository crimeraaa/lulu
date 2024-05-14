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
#define MAX_STACK   256
#define MAX_LINE    256
#define MAX_LOCALS  200
#define MAX_CONSTS  0x1000000
#define MAX_LEVELS  200

// Reserve 6 stack slots for error messages, meaning user-facing stack total is
// more ike `(MAX_STACK - STACK_RESERVED)`.
#define STACK_RESERVED  10

// NUMBER TYPE INFORMATION ------------------------------------------------ {{{1

/**
 * @brief   Arbitrary limit for all tostring-like functions. Must be large
 *          enough to hold the string representation of your desired number type
 *          with its desired precision, as well as any pointer with a typename.
 *
 * @details You may wish to change `MAX_TOSTRING` based on the following:
 *          1.  `NUMBER_FMT` uses a precision larger than currently value or is
 *              longer than the format specification of #2.
 *          2.  The formatted length of `"function: %p", (void*)p"` is greater
 *              than or equal to the current value.
 *
 * @note    The default is 64, and we assume both numbers and pointers fit here.
*/
#define MAX_TOSTRING        64
#define NUMBER_TYPE         double
#define NUMBER_SCAN         "%lf"
#define NUMBER_FMT          "%.14g"
#define num_tostring(s, n)  snprintf((s), MAX_TOSTRING, NUMBER_FMT, (n))
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
#define ptr_tostring(s, p)  snprintf((s), MAX_TOSTRING, "%p", (p))
#define int_tostring(s, i)  snprintf((s), MAX_TOSTRING, "%i", (i))

// 1}}} ------------------------------------------------------------------------

#endif /* LULU_CONFIGURATION_H */
