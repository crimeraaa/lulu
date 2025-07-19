#ifndef LULU_CONFIG_H
#define LULU_CONFIG_H

#define LULU_NUMBER_TYPE    double
#define LULU_NUMBER_FMT     "%.14g"

#define LULU_MEMORY_ERROR_STRING    "out of memory"


/**
 * @brief 2025-06-24
 *  -   Number of stack slots available to all C functions.
 *  -   Indexes 1 up to and including this value are guaranteed to be valid,
 *      thus you do not need to worry about stack overflow.
 */
#define LULU_STACK_MIN      8


/**
 * @brief
 *  -   These macros control the visibility of symbols when building as a
 *      shared library.
 *
 * @brief LULU_PUBLIC
 *  -   Symbols with this attribute are exported.
 *  -   E.g. `LULU_PUBLIC lulu_VM *lulu_open();` can be called even from
 *      outside the shared library.
 *  -   This is useful if compiling with hidden visibility by default, e.g.
 *      the `-fvisibility=hidden` flag in GCC and Clang.
 *
 * @brief LULU_PRIVATE
 *  -   Symbols with this attribute are never exported no matter what.
 *  -   E.g. `LULU_PRIVATE size_t mem_next_size(size_t n);` is callable
 *      only within the shared library.
 *  -   If the user defines an externally visibible symbol with the same name,
 *      there will be no name conflilct and thus no linkage error.
 */
#if defined(__GNUC__) && ((__GNUC__*100 + __GNUC_MINOR__) >= 302) \
    && defined(__ELF__)

/* visibility only matters when building the shared library. */
#ifdef LULU_BUILD_ALL

#define LULU_PUBLIC     __attribute__ ((__visibility__ ("default")))
#define LULU_PRIVATE    __attribute__ ((__visibility__ ("hidden")))

#else   /* ^^^ LULU_BUILD_ALL, otherwise */

#define LULU_PUBLIC
#define LULU_PRIVATE
#endif  /* LULU_BUILD_ALL */

#elif defined(_MSC_VER) /* ^^^ __GNUC__, vvv _MSC_VER */

#if defined(LULU_BUILD_ALL)
#define LULU_PUBLIC     __declspec(dllexport)
#else   /* ^^^ LULU_BUILD_ALL, vvv otherwise */
#define LULU_PUBLIC     __declspec(dllimport)
#endif /* LULU_BUILD_ALL */

/**
 * @brief
 *  -   On Windows, when building DLLs, functions not marked with
 *      `__declspec(dllexport)` are hidden automatically.
 */
#define LULU_PRIVATE

#else /* ^^^ (__GNUC__ && __ELF__) || _MSC_VER, vvv otherwise */

#define LULU_PUBLIC
#define LULU_PRIVATE

#endif /* (__GNUC__ && __ELF__) || _MSC_VER */


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
