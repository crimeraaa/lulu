#pragma once

#include <stdint.h>     /* uint*_t */
#include <stddef.h>     /* size_t */
#include <stdbool.h>    /* bool */

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;

using i32 = int32_t;

#include <assert.h>

#define cast(T, expr)   ((T)(expr))
#define cast_int(expr)  cast(int, expr)
#define unused(expr)    cast(void, expr)

#ifndef __cplusplus
#define nullptr NULL
#endif // __cplusplus
