#pragma once

#include <stdint.h> // uint*_t
#include <stddef.h> // size_t

#include "lulu.h"

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using i32 = int32_t;

#define cast(T)         (T)
#define cast_int(expr)  int(expr)
#define cast_size(expr) size_t(expr)
#define unused(expr)    (void)(expr)

#define count_of(array) (sizeof(array) / sizeof((array)[0]))

#define STRING_FMTSPEC  "%.*s"
#define STRING_QFMTSPEC "'" STRING_FMTSPEC "'"
#define string_fmtarg(s) cast_int(len(s)), raw_data(s)

#define OBJECT_HEADER Object *next; Value_Type type

enum Value_Type {
    VALUE_NIL       = LULU_TYPE_NIL,
    VALUE_BOOLEAN   = LULU_TYPE_BOOLEAN,
    VALUE_NUMBER    = LULU_TYPE_NUMBER,
    VALUE_STRING    = LULU_TYPE_STRING,
    VALUE_TABLE     = LULU_TYPE_TABLE,
    VALUE_FUNCTION  = LULU_TYPE_FUNCTION,

    // Not accessible from user code.
    VALUE_CHUNK,
};

#ifdef LULU_DEBUG

[[gnu::format(printf, 5, 6)]]
void
lulu_assert_(const char *file, int line, bool cond, const char *expr,
    const char *fmt, ...);

#define lulu_assert(expr) \
    lulu_assert_(__FILE__, __LINE__, bool(expr), #expr, nullptr)

#define lulu_assertf(expr, fmt, ...) \
    lulu_assert_(__FILE__, __LINE__, bool(expr), #expr, fmt "\n", __VA_ARGS__)

#define lulu_assertm(expr, msg) \
    lulu_assertf(expr, "%s", msg)

#else

#define lulu_assert(expr)               ((void)0)
#define lulu_assertf(expr, fmt, ...)    ((void)0)
#define lulu_assertm(expr, msg)         ((void)0)

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
inline void
lulu_unreachable()
{}

#endif // __GNUC__ || __clang__ || _MSC_VER

// comma operator shenanigans
#define check_expr(cond, expr)  (lulu_assert(cond), expr)
