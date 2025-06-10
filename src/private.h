#pragma once

#include <stdint.h>     /* uint*_t */
#include <stddef.h>     /* size_t */
#include <stdbool.h>    /* bool */

#include <assert.h>

#define cast(T, expr)   ((T)(expr))
#define cast_int(expr)  cast(int, expr)
#define unused(expr)    cast(void, expr)

#ifndef __cplusplus
#define nullptr NULL
#endif // __cplusplus
