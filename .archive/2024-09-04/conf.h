#ifndef LULU_CONFIGURATION_H
#define LULU_CONFIGURATION_H

#include <limits.h>
#include <math.h>           /* Needed by `num_*` macros. */
#include <stdbool.h>        /* C99 `_Bool` along with alias `bool` */
#include <stddef.h>         /* `size_t`, `nullptr` */
#include <stdlib.h>         /* `realloc`, `free` */
#include <stdio.h>          /* `printf` family, `f*` family (`FILE*`) */
#include <stdint.h>
#include <setjmp.h>         /* `jmp_buf`, `setjmp`, `longjmp` */
#include <string.h>         /* `str*` family, `mem*` family */

/**
 * @brief The following properties are necessary for each macro.
 *
 * @details LULU_BYTE:
 *          Unsigned. Must be large enough to hold all `OpCode` enumerations.
 *          It will also act as the fundamental unit for our bytecode.
 *
 * @details LULU_BYTE2:
 *          Unsigned. Must be large enough to hold 2 `LULU_BYTE`'s.
 *          E.g. if `LULU_BYTE` is 8-bits then this should at least be 16 bits.
 *
 * @details LULU_BYTE3:
 *          Unsigned. Must be large enough to hold 3 `LULU_BYTE`'s.
 *          E.g. if `LULU_BYTE` is 8-bits then this should at least be 24 bits.
 *          You may use 32 bit integers.
 *
 * @details LULU_SBYTE3:
 *          Signed counterpart of `LULU_BYTE3`. We currently use sign-magnitude
 *          representation. That is, we use the lower 23 bits for the "payload"
 *          and the upper 24th bit to determine the signedness.
 *
 *          This has the disadvantage of having separate representations for
 *          +0 and -0, but a jump of 0 should be very bad anyway...
 */
#define LULU_BYTE       uint8_t
#define LULU_BYTE2      uint16_t
#define LULU_BYTE3      uint32_t
#define LULU_SBYTE3     int32_t

#define LULU_PROMPT     "> "
#define LULU_MAX_STACK  256
#define LULU_MAX_LINE   256
#define LULU_MAX_LOCALS 200

/**
 * @details LULU_MAX_CONSTS:
 *          A 24-bit unsigned integer limit. We enforce this as the arguments
 *          to OP_CONSTANT must fit in a Byte3.
 *
 * @details LULU_MAX_LEVELS
 *          An arbitrary limit to prevent the parser from recursing too much.
 *
 * @details LULU_STACK_RESERVED
 *          VM will always have extra stack space for error message formatting
 *          and such.
 */
#define LULU_MAX_CONSTS     16777215
#define LULU_MAX_LEVELS     200
#define LULU_STACK_RESERVED 16

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
#define LULU_MAX_TOSTRING       64
#define LULU_NUMBER_TYPE        double
#define LULU_NUMBER_SCAN        "%lf"
#define LULU_NUMBER_FMT         "%.14g"
#define lulu_num_tostring(s, n) sprintf((s), LULU_NUMBER_FMT, (n))
#define lulu_num_add(a, b)      ((a) + (b))
#define lulu_num_sub(a, b)      ((a) - (b))
#define lulu_num_mul(a, b)      ((a) * (b))
#define lulu_num_div(a, b)      ((a) / (b))
#define lulu_num_mod(a, b)      (fmod(a, b))
#define lulu_num_pow(a, b)      (pow(a, b))
#define lulu_num_unm(a)         (-(a))
#define lulu_num_eq(a, b)       ((a) == (b))
#define lulu_num_lt(a, b)       ((a) <  (b))
#define lulu_num_le(a, b)       ((a) <= (b))
#define lulu_num_isnan(a)       (!num_eq(a, b))

// 1}}} ------------------------------------------------------------------------


// C99-style compound literals have very different semantics in C++.
#ifdef __cplusplus
#define lulu_compound_lit(T, ...)   {__VA_ARGS__}
#else
#define lulu_compound_lit(T, ...)   (T){__VA_ARGS__}
#endif

// Will not work for pointer-decayed arrays.
#define lulu_array_len(arr)         (sizeof((arr)) / sizeof((arr)[0]))
#define lulu_cstr_len(s)            (lulu_array_len(s) - 1)
#define lulu_cstr_eq(a, b, n)       (memcmp((a), (b), (n)) == 0)
#define lulu_cstr_tonumber(s, p)    strtod(s, p)
#define lulu_ptr_tostring(s, p)     sprintf((s), "%p", (p))
#define lulu_int_tostring(s, i)     sprintf((s), "%i", (i))

#ifndef __cplusplus
#define nullptr NULL
#endif /* __cplusplus */

#endif /* LULU_CONFIGURATION_H */
