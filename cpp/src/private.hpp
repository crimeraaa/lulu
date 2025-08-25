#pragma once

#include <inttypes.h> // PRI* macros
#include <stddef.h>   // size_t
#include <stdint.h>   // [u]int*_t

#include "lulu.h"

#define TOKEN_PASTE(x, y)     x##y
#define X__TOKEN_STRINGIFY(x) #x
#define TOKEN_STRINGIFY(x)    X__TOKEN_STRINGIFY(x)
#define SOURCE_CODE_LOCATION  __FILE__ ":" TOKEN_STRINGIFY(__LINE__)

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using i8  = int8_t;
using i32 = int32_t;


/**
 * @brief
 *      Theoretically, this is not enough to represent the full address
 *      space. However in practice most of the address space is invalid
 *      anyway.
 *
 *      E.g. on a 64 bit machine, a platform may only use 48 bits per
 *      address, so signed 64 bit sizes are overkill as it will be
 *      impossible to commit even 1 quadrillion bytes (~50 bits) of memory.
 *      So we assume that this type is more than adequate for our purposes.
 */
using isize = ptrdiff_t;

/**
 * @brief
 *      Only used for consistency with C standard library functions and
 *      allocation functions. Prefer `isize` otherwise.
 */
using usize = size_t;

#define ISIZE_WIDTH     PTRDIFF_WIDTH
#define ISIZE_FMT       "ti"
#define unused(expr)    (void)(expr)
#define size_of(expr)   isize(sizeof(expr))
#define count_of(array) isize(sizeof(array) / sizeof((array)[0]))

#ifndef restrict
#    if defined(__GNUC__) || defined(__clang__)
#        define restrict __restrict__
#    elif defined(_MSC_VER) // ^^^ __GNUC__ || __clang___, vvv _MSC_VER
#        define restrict __restrict
#    else // ^^^ _MSC_VER, vvv else
#        define restrict
#    endif
#endif // restrict


#define OBJECT_HEADER                                                          \
    Object    *next;                                                           \
    Value_Type type

using Type    = lulu_Type;
using Number  = lulu_Number;
using Integer = lulu_Integer;

inline isize
operator""_i(unsigned long long i)
{
    return static_cast<isize>(i);
}

/**
 * @param [out] i
 *      Holds the result of conversion so that the functioon can return
 *      a boolean value to indicate success or failure.
 *
 * @return
 *      true if conversion occured without loss of precision, else false.
 */
inline bool
number_to_integer(Number n, Integer *i)
{
    *i = static_cast<Integer>(n);
    return lulu_Number_eq(static_cast<Number>(*i), n);
}

enum Value_Type {
    VALUE_NIL           = LULU_TYPE_NIL,
    VALUE_BOOLEAN       = LULU_TYPE_BOOLEAN,
    VALUE_LIGHTUSERDATA = LULU_TYPE_LIGHTUSERDATA,
    VALUE_NUMBER        = LULU_TYPE_NUMBER,
    VALUE_STRING        = LULU_TYPE_STRING,
    VALUE_TABLE         = LULU_TYPE_TABLE,
    VALUE_FUNCTION      = LULU_TYPE_FUNCTION,

    // Not accessible from user code.
    VALUE_INTEGER,
    VALUE_CHUNK,
};

#define VALUE_TYPE_LAST VALUE_FUNCTION

static constexpr int VALUE_TYPE_COUNT = VALUE_TYPE_LAST + 1;

template<class T>
inline T
max(T a, T b)
{
    return (a > b) ? a : b;
}

template<class T>
inline void
swap(T *restrict a, T *restrict b)
{
    T tmp = *a;
    *a    = *b;
    *b    = tmp;
}

#if defined(__GNUC__) || defined(__clang__)
#    define lulu_assume(expr) __builtin_assume(expr)
#elif defined(_MSC_VER)
#    define lulu_assume(expr) __assume(expr)
#else
#    define lulu_assume(expr) ((void)0)
#endif

#ifdef LULU_DEBUG

[[noreturn, gnu::format(printf, 3, 4)]] void
lulu_assert_fail(
    const char *where,
    const char *expr,
    const char *fmt,
    ...
) throw();

#    define lulu_assert(expr)                                                  \
        (expr) ? ((void)0)                                                     \
               : lulu_assert_fail(SOURCE_CODE_LOCATION, #expr, nullptr)

#    define lulu_assertf(expr, fmt, ...)                                       \
        (expr) ? ((void)0)                                                     \
               : lulu_assert_fail(                                             \
                   SOURCE_CODE_LOCATION,                                       \
                   #expr,                                                      \
                   fmt "\n",                                                   \
                   __VA_ARGS__                                                 \
               )

#    define lulu_assertln(expr, msg) lulu_assertf(expr, "%s", msg)
#    define lulu_panic()                                                       \
        lulu_assert_fail(SOURCE_CODE_LOCATION, nullptr, nullptr);

#    define lulu_panicf(fmt, ...)                                              \
        lulu_assert_fail(SOURCE_CODE_LOCATION, nullptr, fmt "\n", __VA_ARGS__)

#    define lulu_panicln(msg) lulu_panicf("%s", msg)
#else
#    define lulu_assert(expr)            ((void)0)
#    define lulu_assertf(expr, fmt, ...) ((void)0)
#    define lulu_assertln(expr, msg)     ((void)0)
#    define lulu_panic()                 ((void)0)
#    define lulu_panicf(fmt, ...)        ((void)0)
#    define lulu_panicln(msg)            ((void)0)
#endif // LULU_DEBUG

// `lulu_unreachable()` must be defined regardless of `LULU_DEBUG`.
#if defined(__GNUC__) || defined(__clang__)
#    define lulu_unreachable() __builtin_unreachable()
#elif defined(_MSC_VER) // ^^^ __GNUC__ || __clang__, vvv _MSC_VER
#    define lulu_unreachable() __assume(false)
#else // ^^^ _MSC_VER, vvv anything else

// Do not implement as a function-like macro `lulu_assert(false)` because
// `lulu_assert()` itself may be empty.
[[noreturn]] inline void
lulu_unreachable()
{}

#endif // __GNUC__ || __clang__ || _MSC_VER
