#ifndef LULU_CONFIG_H
#define LULU_CONFIG_H

#include <limits.h>
#include <stddef.h>

#define LULU_NUMBER_TYPE  double
#define LULU_NUMBER_FMT   "%.14g"
#define LULU_INTEGER_TYPE ptrdiff_t


/**
 * @brief CONFIG:
 *      The size of a `char` array needed when writing `lulu_Number` to a
 *      string via `sprintf()`.
 *
 * @details
 *      Consider the format string `"%.14g"`.
 *
 *      According to `man 3 printf`, since our given precision is 14, we
 *      have that many decimal digits. We must also add 1 for the decimal
 *      (radix) point `.`. This brings us to at least size 15.
 *
 *      If the exponent of the given value is +/-4 of the given precision,
 *      then the `e` style is used, e.g. in `6.023e24`. Thus, we need to
 *      also consider the `e` specifier, a possible sign `[+-]`, and the
 *      exponent. This brings us to at least size 17.
 *
 *      For simplicity and to allow some legroom, we use the next power of 2
 *      which is 32.
 */
#define LULU_NUMBER_BUFSIZE 32


/**
 * @brief CONFIG:
 *      The size of the fixed array in `lulu_auxlib.h:lulu_Buffer`. This
 *      allows us to 'hold' onto as much data as we can before we absolutely
 *      have to 'flush' it (i.e. pushing the resulting string to the top of
 *      the stack).
 *
 * @note(2025-07-20)
 *
 *      This requires `stdio.h` to be included beforehand; we do not include
 *      it for you in this header to avoid unnecessary namespace pollution.
 */
#define LULU_BUFFER_BUFSIZE BUFSIZ


/**
 * @brief CONFIG:
 *      The message pushed to the top of the stack when a memory allocation
 *      request cannot be fulfilled. It is interned on VM startup, so if
 *      `lulu_open()` returned a non-`NULL` pointer then it can be safely
 *      assumed that this string will always be retrievable.
 */
#define LULU_MEMORY_ERROR_STRING "out of memory"


/**
 * @brief(2025-06-24) CONFIG:
 *      Number of stack slots available to all C functions. Indexes 1 up
 *      to and including this value are guaranteed to be valid, thus you do
 *      not need to worry about stack overflow.
 */
#define LULU_STACK_MIN 8


#define LULU_BASE_LIB_NAME   "base"
#define LULU_STRING_LIB_NAME "string"
#define LULU_MATH_LIB_NAME   "math"
#define LULU_OS_LIB_NAME     "os"


/**
 * @brief CONFIG:
 *      These macros control the visibility of symbols when building as a
 *      shared library.
 *
 * @brief LULU_PUBLIC
 *      Symbols with this attribute are exported.
 *      E.g. `LULU_PUBLIC lulu_VM *lulu_open();` can be called even from
 *      outside the shared library. This is useful if compiling with hidden
 *      visibility by default, e.g. the `-fvisibility=hidden` flag in GCC
 *      and Clang.
 *
 * @brief LULU_PRIVATE
 *      Symbols with this attribute are never exported no matter what.
 *      E.g. `LULU_PRIVATE size_t mem_next_size(size_t n);` is callable
 *      only within the shared library. If the user defines an externally
 *      visibible symbol with the same name, there will be no name
 *      conflilct and thus no linkage error.
 */
#if defined(__GNUC__) && ((__GNUC__ * 100 + __GNUC_MINOR__) >= 302)            \
    && defined(__ELF__)

/* visibility only matters when building the shared library. */
#    ifdef LULU_BUILD_ALL
#        define LULU_PUBLIC  __attribute__((__visibility__("default")))
#        define LULU_PRIVATE __attribute__((__visibility__("hidden")))
#    else /* ^^^ LULU_BUILD_ALL, otherwise */
#        define LULU_PUBLIC
#        define LULU_PRIVATE
#    endif              /* LULU_BUILD_ALL */
#elif defined(_MSC_VER) /* ^^^ __GNUC__, vvv _MSC_VER */
#    if defined(LULU_BUILD_ALL)
#        define LULU_PUBLIC __declspec(dllexport)
#    else /* ^^^ LULU_BUILD_ALL, vvv otherwise */
#        define LULU_PUBLIC __declspec(dllimport)
#    endif /* LULU_BUILD_ALL */

/**
 * @brief
 *      On Windows, when building DLLs, functions not marked with
 *      `__declspec(dllexport)` are hidden automatically.
 */
#    define LULU_PRIVATE
#else /* ^^^ (__GNUC__ && __ELF__) || _MSC_VER, vvv otherwise */
#    define LULU_PUBLIC
#    define LULU_PRIVATE
#endif /* LULU_PUBLIC, LULU_PRIVATE */


#ifdef __cplusplus
/**
 * @brief CONFIG:
 *      When compiling the shared libary, we use a C++ compiler. So disable
 *      name mangling to allow C applications to link to us properly.
 */
#    define LULU_API extern "C" LULU_PUBLIC
#else /* ^^^ __cplusplus, vvv otherwise */
#    define LULU_API extern LULU_PUBLIC
#endif /* __cplusplus */


/**
 * @brief CONFIG:
 *      Lua library functions generally have the same conventions as the
 *      Lua API. However, should you need something different (e.g. no
 *      visibility attribute, no extern C), you can change it.
 */
#define LULU_LIB_API LULU_API


/**
 * @brief CONFIG:
 *      Controls the visibility of externally visible functions that
 *      are not part of the API.
 *
 *      That is, you can define a function like so: `LULU_FUNC void f();`
 *      `f` will not be exported but it is still visible to all functions
 *      within the library that include `f`'s header.
 */
#define LULU_FUNC extern LULU_PRIVATE


/**
 * @brief CONFIG:
 *      Similar to `LULU_FUNC`, but intended for data. e.g. `LULU_DATA const
 *      char *const tokens[TOKEN_COUNT];`
 */
#define LULU_DATA extern LULU_PRIVATE

#if defined(__GNUC__) || defined(__clang__)

/**
 * @brief CONFIG:
 *      The GNU C format attribute allows compile-time checking of functions
 *      which take printf-style arguments. The Lulu API nor the string
 *      library never fully implement printf completely; see the
 *      documentation for each specific function's limitations.
 *
 * @param fmt
 *      The 1-based index of the format argument in the parameter list.
 *
 * @param arg
 *      The 1-based index of the variadic arguments in the parameter list.
 */
#    define LULU_ATTR_PRINTF(fmt, arg)                                         \
        __attribute__((__format__(printf, fmt, arg)))
#else /* ^^^ __GNUC__ || __clang__, vvv otherwise */
#    define LULU_ATTR_PRINTF(fmt, arg)
#endif /* LULU_ATTR_PRINTF */

#ifdef LULU_BUILD_ALL

#    include <math.h>

#    define lulu_Number_add(x, y) ((x) + (y))
#    define lulu_Number_sub(x, y) ((x) - (y))
#    define lulu_Number_mul(x, y) ((x) * (y))
#    define lulu_Number_div(x, y) ((x) / (y))
#    define lulu_Number_mod(x, y) fmod(x, y)
#    define lulu_Number_pow(x, y) pow(x, y)
#    define lulu_Number_unm(x)    (-(x))

#    define lulu_Number_eq(x, y)  ((x) == (y))
#    define lulu_Number_lt(x, y)  ((x) < (y))
#    define lulu_Number_leq(x, y) ((x) <= (y))

#endif /* LULU_BUILD_ALL */

/**=== LOCAL REDEFINITIONS ============================================= {{{
 * You can `#undef` any of the above macros, mainly the ones marked with
 * 'CONFIG:' and redefine them to your liking.
 *======================================================================= */

/* #undef LULU_BUFFER_BUFSIZE
#define LULU_BUFFER_BUFSIZE 16 */

/*=== }}} =============================================================== */

#endif /* LULU_CONFIG_H */
