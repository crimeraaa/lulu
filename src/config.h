#ifndef LULU_CONFIG_H
#define LULU_CONFIG_H

#define LULU_NUMBER_TYPE    double
#define LULU_NUMBER_FMT     "%.14g"

/**
 * @brief 2025-06-24
 *  -   Number of stack slots available to all C functions.
 *  -   Indexes 1 up to and including this value are guaranteed to be valid,
 *      thus you do not need to worry about stack overflow.
 */
#define LULU_STACK_MIN      8

#ifdef LULU_BUILD_ALL

#include <math.h>

#define lulu_Number_add(x, y)   ((x) + (y))
#define lulu_Number_sub(x, y)   ((x) - (y))
#define lulu_Number_mul(x, y)   ((x) * (y))
#define lulu_Number_div(x, y)   ((x) / (y))
#define lulu_Number_mod(x, y)   fmod(x, y)
#define lulu_Number_pow(x, y)   pow(x, y)
#define lulu_Number_unm(x)      (-(x))

#define lulu_Number_eq(x, y)    ((x) == (y))
#define lulu_Number_lt(x, y)    ((x) < (y))
#define lulu_Number_leq(x, y)   ((x) <= (y))

#endif /* LULU_BUILD_ALL */

#endif /* LULU_CONFIG_H */
