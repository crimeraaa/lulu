#pragma once

#include <stdint.h> /* uint*_t */
#include <stddef.h> /* size_t */

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using i32 = int32_t;

#define cast(T, expr)   ((T)(expr))
#define cast_int(expr)  cast(int, expr)
#define unused(expr)    cast(void, expr)

#define count_of(array) (sizeof(array) / sizeof((array)[0]))

#ifdef LULU_DEBUG

extern "C" void
lulu_assert(const char *file, int line, bool cond, const char *expr);

#define lulu_assert(expr) \
    lulu_assert(__FILE__, __LINE__, cast(bool, expr), #expr)

#else

#define lulu_assert(expr)

#endif // LULU_DEBUG

#ifndef __cplusplus
#define nullptr NULL
#endif // __cplusplus
