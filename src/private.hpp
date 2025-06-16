#pragma once

#include <stdint.h> // uint*_t
#include <stddef.h> // size_t

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using i32 = int32_t;

#define cast(T, expr)   ((T)(expr))
#define cast_int(expr)  cast(int, expr)
#define unused(expr)    cast(void, expr)

#define count_of(array) (sizeof(array) / sizeof((array)[0]))

#ifdef LULU_DEBUG

[[gnu::format(printf, 5, 6)]]
void
lulu_assert_(const char *file, int line, bool cond, const char *expr,
    const char *fmt, ...);

#define lulu_assert(expr) \
    lulu_assert_(__FILE__, __LINE__, cast(bool, expr), #expr, nullptr)

#define lulu_assertf(expr, fmt, ...) \
    lulu_assert_(__FILE__, __LINE__, cast(bool, expr), #expr, fmt "\n", __VA_ARGS__)

#define lulu_assertm(expr, msg) \
    lulu_assertf(expr, "%s", msg)

#else

#define lulu_assert(expr)
#define lulu_assertf(expr, fmt, ...)
#define lulu_assertm(expr, msg)

#endif // LULU_DEBUG

// `lulu_unreachable()` must be defined regardless of `LULU_DEBUG`.
#if defined(__GNUC__) || defined(__clang__)

#define lulu_unreachable()  __builtin_unreachable()

#elif defined(_MSC_VER) // ^^^ __GNUC__ || __clang__, vvv _MSC_VER

#define lulu_unreachable()  __assume(false)

#else // ^^^ _MSC_VER, vvv anything else

[[noreturn]]
void
lulu_unreachable();

#endif // __GNUC__ || __clang__ || _MSC_VER


#ifndef __cplusplus
#define nullptr NULL
#endif // __cplusplus
