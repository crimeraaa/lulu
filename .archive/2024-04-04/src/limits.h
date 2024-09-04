#ifndef LUA_LIMITS_H
#define LUA_LIMITS_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include "lua.h"

typedef uint8_t     Byte;     // Smallest addressable natural number.
typedef uint32_t    Int32;    // To be used for instruction sizes.
typedef size_t      lua_UMem; // Unsigned type for memory usage.
typedef ptrdiff_t   lua_SMem; // Signed equivalent of `lua_UMem`.

#define LUA_MAXTYPE(T)      (~(T)0)

// Subtract 2 for a little bit of safety.
#define LUA_MAXSIZET        (size_t)(LUA_MAXTYPE(size_t) - 2)
#define LUA_MAXUMEM         (lua_UMem)(LUA_MAXTYPE(lua_UMem) - 2)
#define LUA_MAXINT          (INT_MAX - 2)
#define LUA_MAXBYTE         (Byte)(LUA_MAXTYPE(Byte) - 2)

// Silence unused variable/parameter warnings.
#define unused(exp)         ((void)(exp))
#define unused2(exp, ...)   unused(exp); unused(__VA_ARGS__)
#define unused3(exp, ...)   unused(exp); unused2(__VA_ARGS__)

#define cast(T, exp)        ((T)(exp))
#define cast_byte(N)        cast(Byte, (N))
#define cast_number(N)      cast(lua_Number, (N))
#define cast_int(N)         cast(int, (N))

// We can't use `cast` since `exp` is surrounded by parentheses there.
#define compoundlit(T, ...) ((T){__VA_ARGS__})

// Quickly create a C99 compound literal and cast it to be an array.
#define arraylit(T, ...)    compoundlit(T[], __VA_ARGS__)

// NOTE: Will not work for arrays that decayed to pointers!
#define arraylen(array)     (sizeof((array)) / sizeof((array)[0]))
#define arraysize(T, N)     (sizeof(T) * N)

#if defined(DEBUG_USE_ASSERT)

/**
 * @brief   Assert `cond` then evaluate `expr`. If this is part of an assignment
 *          then `expr` will be returned due to how the comma operator works.
 */
#define check_exp(cond, expr)   (assert(cond), expr)

#else /* DEBUG_USE_ASSERT not defined. */

/**
 * @brief   Ignore `cond` and immediately evaluate `expr`.
 */
#define check_exp(cond, expr)   (expr)

#endif /* DEBUG_USE_ASSERT */

#define api_check(vm, expr)     luai_apicheck

/** 
 * @brief   Type for virtual machine instructions. It must be an unsigned 32-bit
 *          integer in order to fit the A, B and C registers plus an opcode.
 *
 * @note    See `opcodes.h` for more information on how the registers work.
 */
typedef Int32 Instruction;

#define LUA_MAXINSTRUCTION      LUA_MAXTYPE(Instruction)

// Maximum stack size for a Lua function.
#define LUA_MAXSTACK    250

// Minimum length for a string buffer.
#define LUA_MINBUFFER   32

#endif /* LUA_LIMITS_H */
