#pragma once

#define TOKEN_PASTE(x, y)     x##y
#define X__TOKEN_STRINGIFY(x) #x
#define TOKEN_STRINGIFY(x)    X__TOKEN_STRINGIFY(x)
#define SOURCE_CODE_LOCATION  __FILE__ ":" TOKEN_STRINGIFY(__LINE__)

#if defined(__GNUC__) || defined(__clang__)
#   define lulu_assume(expr)        __builtin_assume(expr)
#   ifdef LULU_DEBUG
#       define lulu_unreachable()   __builtin_trap()
#   else
#       define lulu_unreachable()   __builtin_unreachable()
#   endif
#   define restrict __restrict__
#elif defined(_MSC_VER)
#   define lulu_assume(expr)        __assume(expr)
#   ifdef LULU_DEBUG
// https://learn.microsoft.com/en-us/cpp/intrinsics/debugbreak?view=msvc-170
#       define lulu_unreachable()   __debugbreak()
#   else
#       define lulu_unreachable()   __assume(false)
#   endif
#   define restrict __restrict
#endif

#ifdef LULU_DEBUG

[[noreturn, gnu::format(printf, 3, 4)]] void
lulu_assert_fail(const char *where, const char *expr, const char *fmt,
    ...) throw();

#   define lulu_assert_fail(expr, ...) lulu_assert_fail(SOURCE_CODE_LOCATION, expr, __VA_ARGS__)
#else
#   define lulu_assert_fail(expr, ...) ((void)0)
#endif

#define lulu_assert(expr)            if (!bool(expr)) lulu_assert_fail(#expr, nullptr)
#define lulu_assertf(expr, fmt, ...) if (!bool(expr)) lulu_assert_fail(#expr, fmt "\n", __VA_ARGS__)
#define lulu_assertln(expr, msg)     lulu_assertf(expr, "%s", msg)

// Unconditionally assert.
#define lulu_panic_fail(...)  lulu_assert_fail(nullptr, __VA_ARGS__)
#define lulu_panic()          lulu_panic_fail(nullptr)
#define lulu_panicf(fmt, ...) lulu_panic_fail(fmt "\n", __VA_ARGS__)
#define lulu_panicln(msg)     lulu_panicf("%s", msg)

//=== FALLBACKS ======================================================== {{{

#ifndef lulu_assume
#   define lulu_assume(expr)    ((void)0)
#endif

#ifndef lulu_unreachable
// Do not implement as a function-like macro `lulu_assert(false)` because
// `lulu_assert()` itself may be empty.
[[noreturn]] inline void
lulu_unreachable()
{}
#endif

#ifndef restrict
#define restrict
#endif

//=== }}} ==================================================================
