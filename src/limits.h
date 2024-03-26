#ifndef LUA_LIMITS_H
#define LUA_LIMITS_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include "lua.h"

typedef uint32_t    lua_Int32;
typedef size_t      lua_UMem; // Unsigned type for memory usage.
typedef ptrdiff_t   lua_SMem; // Signed equivalent of `lua_UMem`.

// Used as a small natural number, `char` itself is reserved for characters.
typedef unsigned char Byte;

// Subtract 2 for a little bit of safety.
#define LUA_MAXSIZET    (size_t)((~(size_t)0) - 2)
#define LUA_MAXUMEM     (lua_UMem)((~(lua_UMem)0) - 2)
#define LUA_MAXINT      (INT_MAX - 2)
#define LUA_MAXBYTE     (Byte)((~(Byte)0) - 2)

// Silence unused variable/parameter warnings.
#define unused(exp)     ((void)(exp))
#define cast(T, exp)    ((T)(exp))
#define cast_byte(N)    cast(Byte, (N))
#define cast_number(N)  cast(lua_Number, (N))
#define cast_int(N)     cast(int, (N))

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
typedef lua_Int32 Instruction;

#define LUA_MAXINSTRUCTION      (~(Instruction)0)

// Maximum stack size for a Lua function.
#define LUA_MAXSTACK    250

// Minimum length for a string buffer.
#define LUA_MINBUFFER   32

#endif /* LUA_LIMITS_H */
