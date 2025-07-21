#pragma once

#include <stdint.h>     // [u]int*_t
#include <stddef.h>     // size_t
#include <inttypes.h>   // PRI* macros

#include "lulu.h"

#define TOKEN_PASTE(x, y)   x##y

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using i32 = int32_t;


/**
 * @brief
 *  -   Theoretically, this is not enough to represent the full address space.
 *  -   However in practice most of the address space is invalid anyway.
 *  -   E.g. on a 64 bit machine, a platform may only use 48 bits per address,
 *      so signed 64 bit sizes are overkill as it will be impossible to commit
 *      even 1 quadrillion bytes (~50 bits) of memory.
 *  -   So we assume that this type is more than adequate for our purposes.
 */
using isize = ptrdiff_t;

/**
 * @brief
 *  -   Only used for consistency with C standard library functions and
 *      allocation functions.
 *  -   Prefer `isize` otherwise.
 */
using usize = size_t;

#define ISIZE_FMTSPEC       "ti"

#define cast(T)             (T)
#define cast_int(expr)      int(expr)
#define cast_isize(expr)    isize(expr)
#define cast_usize(expr)    usize(expr)
#define unused(expr)        (void)(expr)

#define size_of(expr)   isize(sizeof(expr))
#define count_of(array) isize(sizeof(array) / sizeof((array)[0]))

#define OBJECT_HEADER Object *next; Value_Type type

using Type   = lulu_Type;
using Number = lulu_Number;

enum Value_Type {
    VALUE_NIL       = LULU_TYPE_NIL,
    VALUE_BOOLEAN   = LULU_TYPE_BOOLEAN,
    VALUE_NUMBER    = LULU_TYPE_NUMBER,
    VALUE_STRING    = LULU_TYPE_STRING,
    VALUE_TABLE     = LULU_TYPE_TABLE,
    VALUE_FUNCTION  = LULU_TYPE_FUNCTION,
    VALUE_USERDATA  = LULU_TYPE_USERDATA,

    // Not accessible from user code.
    VALUE_CHUNK,
};

static constexpr int
VALUE_TYPE_COUNT = VALUE_USERDATA + 1;

template<class T>
LULU_FUNC inline void
swap(T *a, T *b)
{
    T tmp = *a;
    *a = *b;
    *b = tmp;
}

#ifdef LULU_DEBUG

[[noreturn, gnu::format(printf, 4, 5)]]
LULU_FUNC void
lulu_assert_fail(const char *file, int line, const char *expr, const char *fmt, ...)
throw();

#define lulu_assert(expr) \
    (expr) ? ((void)0) : lulu_assert_fail(__FILE__, __LINE__, #expr, nullptr)

#define lulu_assertf(expr, fmt, ...) \
    (expr) ? ((void)0) : lulu_assert_fail(__FILE__, __LINE__, #expr, fmt "\n", __VA_ARGS__)

#define lulu_assertln(expr, msg) \
    lulu_assertf(expr, "%s", msg)

#else

#define lulu_assert(expr)               ((void)0)
#define lulu_assertf(expr, fmt, ...)    ((void)0)
#define lulu_assertln(expr, msg)        ((void)0)

#endif // LULU_DEBUG

// `lulu_unreachable()` must be defined regardless of `LULU_DEBUG`.
#if defined(__GNUC__) || defined(__clang__)

#define lulu_unreachable()  __builtin_unreachable()

#elif defined(_MSC_VER) // ^^^ __GNUC__ || __clang__, vvv _MSC_VER

#define lulu_unreachable()  __assume(false)

#else // ^^^ _MSC_VER, vvv anything else

// Do not implement as a function-like macro `lulu_assert(false)` because
// `lulu_assert()` itself may be empty.
[[noreturn]]
LULU_FUNC inline void
lulu_unreachable()
{}

#endif // __GNUC__ || __clang__ || _MSC_VER
