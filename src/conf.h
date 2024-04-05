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
#include <setjmp.h>         /* `jmp_buf`, `setjmp`, `longjmp` */
#include <string.h>         /* `str*` family, `mem*` family */

// Must be large enough to hold all `OpCode` enumerations as well as act as part
// of each instruction's operand. This MUST be unsigned.
typedef uint8_t     Byte;
typedef uint16_t    Byte2;
typedef uint32_t    Byte3; // We only need 24 bits at most but this will do.

#define LULU_PROMPT         "> "
#define LULU_MAXSTACK       256
#define LULU_MAXLINE        256

/* --- NUMBER TYPE INFORMATION -------------------------------------------- {{{1
You may wish to change `LULU_MAXNUM2STR` based on the following conditions:
1.  `LULU_NUMBER_FMT` uses a precision larger 64 digits or is longer than the format
    specification of #2.
2.  The formatted length of `"function: %p", (void*)p"` is greater than or equal
    to 64 characters.
*/

#define LULU_NUMBER_TYPE        double
#define LULU_NUMBER_SCAN        "%lf"
#define LULU_NUMBER_FMT         "%.14g"
#define LULU_MAXNUM2STR         64
#define lulu_numtostring(s, n)  snprintf((s), LULU_MAXNUM2STR, NUMBER_FMT, (n))
#define lulu_strtonumber(s, p)  strtod(s, p)
#define lulu_numadd(a, b)       ((a) + (b))
#define lulu_numsub(a, b)       ((a) - (b))
#define lulu_nummul(a, b)       ((a) * (b))
#define lulu_numdiv(a, b)       ((a) / (b))
#define lulu_nummod(a, b)       (fmod(a, b))
#define lulu_numpow(a, b)       (pow(a, b))
#define lulu_numunm(a)          (-(a))
#define lulu_numeq(a, b)        ((a) == (b))
#define lulu_numlt(a, b)        ((a) <  (b))
#define lulu_numle(a, b)        ((a) <= (b))
#define lulu_numisnan(a)        (!lulu_numeq(a, b))

// 1}}} ------------------------------------------------------------------------

#endif /* LULU_CONFIGURATION_H */
