/**
 * @brief   MAX_* macros and other internal use/helper macros. These are not
 *          intended to be configured or used by the host/end-user.
 */
#ifndef LULU_LIMITS_H
#define LULU_LIMITS_H

#include "lulu.h"

#define IS_ENABLED_PLACEHOLDER_1 0,

/**
 * @brief   Helps perform dead-code elimination and less reliance on `#ifdef`
 *          inside of `.c` files.
 *
 * @details Step 1: Expand the argument to its value (preferably `1`), else
 *          expand to the token itself. E.g. given `#define CONF 1`, the call
 *          `is_enabled(CONF)` will expand to `_is_enabled_a(1)`.
 *
 * @note    See: https://elixir.bootlin.com/linux/v4.4/ident/IS_ENABLED
 */
#define is_enabled(config) _is_enabled_a(config)

/**
 * @details Step 2: Given the expanded config previously, try to concatenate it
 *          to the token `_arg_placeholder_`. E.g. if we got `1`, we would
 *          receive `IS_ENABLED_PLACEHOLDER_1` which will be cherry picked in the next
 *          step. Otherwise the resulting token will not be expandable and end
 *          up cherry picking 0.
 */
#define _is_enabled_a(config) _is_enabled_b(IS_ENABLED_PLACEHOLDER_##config)

/**
 * @details Step 3: If the macro expands to anything, the expansion will now be
 *          `_is_enabled_c({expansion}, 1, 0)`.
 *
 *          In the case of macros explicitly defined to be 1, we will insert
 *          IS_ENABLED_PLACEHOLDER_1. Otherwise insert the original expansion as-is.
 *
 *          If there is no expansion, then we simply have `_is_enabled_c(1, 0)`.
 */
#define _is_enabled_b(arg_or_junk) _is_enabled_c(arg_or_junk 1, 0)

/**
 * @details Step 4: Depending on the result of the previous steps, we will have
 *          either `(0, 1, 0)` or `({expansion}, 1, 0)` or simply `(1, 0)`.
 *          Whatever the case, we will cherry pick the second argument.
 */
#define _is_enabled_c(ignore, val, ...) val

#ifdef LULU_DEBUG_ASSERT
#include <assert.h>
#else /* LULU_DEBUG_ASSERT not defined. */

// Hack because void statements in global scope are disallowed.
#define assert(expr)                struct _silence_stray_semicolon_warning
#define static_assert(expr, info)   assert(expr)

#endif /* LULU_DEBUG_ASSERT */

typedef LULU_BYTE   Byte;
typedef LULU_BYTE2  Byte2;
typedef LULU_BYTE3  Byte3;
typedef LULU_SBYTE3 SByte3;

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
#define array_len(array)    lulu_array_len(array)
#define array_size(T, N)    (sizeof(T) * (N))
#define array_lit(T, ...)   lulu_compound_lit(T[], __VA_ARGS__)

// Use when `P` is a pointer to a non-void type. Useful to make allocations
// a little more robust by relying on pointer for sizeof instead of type.
#define parray_size(P, N)   (sizeof(*(P)) * (N))

// Get the number of bits that `N` bytes holds.
#define bit_count(N)        ((N) * BITS_PER_BYTE)
#define bit_size(T)         bit_count(sizeof(T))

#ifdef __cplusplus
#define cast(T, expr)       static_cast<T>(expr)
#else /* __cplusplus not defined. */
#define cast(T, expr)       ((T)(expr))
#endif /* __cplusplus */

#define cast_int(expr)      cast(int, expr)
#define cast_num(expr)      cast(lulu_Number, expr)
#define cast_byte(expr)     cast(Byte, expr)

#define unused(x)           cast(void, x)
#define unused2(x, y)       unused(x),     unused(y)
#define unused3(x, y, z)    unused2(x, y), unused(z)

/**
 * @details MAX_BYTE:
 *          0b11111111
 * @details MAX_BYTE2:
 *          0b11111111_11111111
 * @details MAX_BYTE3:
 *          0b11111111_11111111_11111111_11111111
 * @details MAX_SBYTE3:
 *          0b01111111_11111111_11111111_11111111
 * @details MIN_SBYTE3:
 *          0b10000000_00000000_00000000_00000000
 */
#define MAX_BYTE            cast(Byte,  -1)
#define MAX_BYTE2           cast(Byte2, -1)
#define MAX_BYTE3           ((1 << bit_count(3)) - 1)
#define MAX_SBYTE3          (MAX_BYTE3 >> 1)
#define MIN_SBYTE3          (~MAX_SBYTE3)

#define cstr_len(s)         lulu_cstr_len(s)
#define cstr_eq(a, b, n)    lulu_cstr_eq(a, b, n)
#define cstr_tonumber(s, p) lulu_cstr_tonumber(s, p)

#define in_incrange(n, lo, hi)      ((n) >= (lo) && (n) <= (hi))
#define in_excrange(n, lo, hi)      ((n) >= (lo) && (n) < (hi))

typedef struct {
    const char *string; // Pointer to the first character.
    size_t      length; // How many valid characters are being pointed at.
} LString;

#define lstr_from_len(s, len)    (LString){(s), (len)}
#define lstr_from_end(s, end)    (LString){(s), (end) - (s)}
#define lstr_from_lit(s)         (LString){(s), cstr_len(s)}

#endif /* LULU_LIMITS_H */
