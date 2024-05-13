/**
 * @brief   MAX_* macros and other internal use/helper macros. These are not
 *          intended to be configured or used by the host/end-user.
 */
#ifndef LULU_LIMITS_H
#define LULU_LIMITS_H

#include "lulu.h"

#ifdef DEBUG_USE_ASSERT
#include <assert.h>
#else /* DEBUG_USE_ASSERT not defined. */

#define assert(expr)                struct _silence_stray_semicolon_warning
#define static_assert(expr, info)   assert(expr)

#endif /* DEBUG_USE_ASSERT */

#define BITS_PER_BYTE       CHAR_BIT

#define eprintln(s)         fputs(s "\n", stderr)
#define eprintf(s, ...)     fprintf(stderr, s, __VA_ARGS__)
#define eprintfln(s, ...)   eprintf(s "\n", __VA_ARGS__)

#define _stringify(x)       #x
#define stringify(x)        _stringify(x)
#define logformat(s)        __FILE__ ":" stringify(__LINE__) ": " s
#define logprintln(s)       eprintln(logformat(s))
#define logprintf(s, ...)   eprintf(logformat(s), __VA_ARGS__)
#define logprintfln(s, ...) eprintfln(logformat(s), __VA_ARGS__)

// Dangerous to use this for C++, so be careful!
#define compoundlit(T, ...) (T){__VA_ARGS__}

// Will not work for pointer-decayed arrays.
#define array_len(array)    (sizeof((array)) / sizeof((array)[0]))
#define array_size(T, N)    (sizeof(T) * (N))
#define array_lit(T, ...)   compoundlit(T[], __VA_ARGS__)

// Use when `P` is a pointer to a non-void type. Useful to make allocations
// a little more robust by relying on pointer for sizeof instead of type.
#define parray_size(P, N)   (sizeof(*(P)) * (N))

// Helper macro for functions that expects varargs of the same type.
#define vargs_count(T, ...) (sizeof(array_lit(T, __VA_ARGS__)) / sizeof(T))

// Get the number of bits that `N` bytes holds.
#define bytes_to_bits(N)    ((N) * BITS_PER_BYTE)
#define bitsize(T)          bytes_to_bits(sizeof(T))

#define cast(T, expr)       ((T)(expr))
#define unused(x)           cast(void, x)
#define unused2(x, y)       unused(x); unused(y)
#define unused3(x, y, z)    unused2(x, y); unused(z)

// String literal length. Useful for expressions needed at compile-time.
#define cstr_len(s)         (array_len(s) - 1)
#define cstr_eq(a, b, n)    (memcmp(a, b, n) == 0)

#define MAX_BYTE            cast(Byte,  -1)
#define MAX_BYTE2           cast(Byte2, -1)
#define MAX_BYTE3           ((1 << bytes_to_bits(3)) - 1)

typedef enum {
    ERROR_NONE,
    ERROR_COMPTIME,
    ERROR_RUNTIME,
    ERROR_ALLOC,
} ErrType;

typedef struct {
    const char *begin; // Pointer to the first character in the string.
    const char *end;   // Pointer to nul character or 1 past last valid index.
    int         len;   // What the result of `strlen` would be.
} StrView;

// Silly to populate both end and len like this but we need consistency.
#define strview_lit(s)      {(s), &(s)[0] + cstr_len(s), cstr_len(s)}
#define make_strview(s, n)  (StrView){(s), (s) + (n), (n)}

#endif /* LULU_LIMITS_H */
