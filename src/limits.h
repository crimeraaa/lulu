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

#define assert(expr)
#define static_assert(expr)

#endif /* DEBUG_USE_ASSERT */

#define _stringify(x)       #x
#define stringify(x)        _stringify(x)
#define loginfo()           __FILE__ ":" stringify(__LINE__)
#define logformat(s)        loginfo() ": " s
#define logprintln(s)       fputs(logformat(s) "\n", stderr)
#define logprintf(s, ...)   fprintf(stderr, logformat(s), __VA_ARGS__)
#define logprintfln(s, ...) logprintf(s "\n", __VA_ARGS__)

#define BITS_PER_BYTE       CHAR_BIT

#define compoundlit(T, ...) (T){__VA_ARGS__}

// Will not work for pointer-decayed arrays.
#define arraylen(array)     (sizeof(array) / sizeof(array[0]))
#define arraysize(T, N)     (sizeof(T) * (N))
#define arraylit(T, ...)    compoundlit(T[], __VA_ARGS__)

// Get the number of bits that `N` bytes holds.
#define bytes_to_bits(N)    ((N) * BITS_PER_BYTE)
#define bitsize(T)          bytes_to_bits(sizeof(T))

#define cast(T, expr)       (T)(expr)
#define unused(x)           (void)(x)
#define unused2(x, y)       unused(x); unused(y)
#define unused3(x, y, z)    unused2(x, y); unused(z)

// String literal length. Useful for expressions needed at compile-time.
#define cstr_litsize(s)     (arraylen(s) - 1)
#define cstr_equal(a, b, n) (memcmp(a, b, n) == 0)

#define MAX_BYTE            cast(Byte,  -1)
#define MAX_BYTE2           cast(Byte2, -1)
#define MAX_BYTE3           ((1 << bytes_to_bits(3)) - 1)

#endif /* LULU_LIMITS_H */
