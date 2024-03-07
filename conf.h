#ifndef LUA_CONFIGURATION_H
#define LUA_CONFIGURATION_H

#ifdef _WIN32
#define LUA_DIRSEP "\\"
#else /* _WIN32 not defined. */
#define LUA_DIRSEP "/"
#endif

#include <math.h>       /* floor, fmod, pow */
#include <stddef.h>     /* size_t, ptrdiff_t, NULL */
#include <stdint.h>     /* uint*_t family */

/* --- INTERNAL IMPLEMENTATION -------------------------------------------- {{{ 
 These are internal implementation details. Users/scripts must not rely on this.
 We specify things like integer width and format specifications. Set these to 
 the exact types appropriate for your system.

 The sizes MUST be in the following order, from smallest to largest:
 
 Byte         < Word         < DWord         < QWord
 sizeof(Byte) < sizeof(Word) < sizeof(DWord) < sizeof(QWord)
 
 And the following must be true:

 sizeof(Word)  == (sizeof(Byte)  * 2)
 sizeof(DWord) == (sizeof(Word)  * 2)
 sizeof(QWord) == (sizeof(DWord) * 2)
 */

typedef uint8_t   Byte;  // Smallest addressable size. Usually 8-bits.
typedef uint16_t  Word;  // 2 `Byte`s wide type. Usually 16-bits.
typedef uint32_t  DWord; // 2 `Word`s long. Usually 32-bits.
typedef uint64_t  QWord; // 4 `Word`s long. Usually 64-bits.

/* }}} ---------------------------------------------------------------------- */

/**
 * This is the default stack-allocated size of the REPL's char buffer.
 * We prefer stack-allocated over heap-allocated because it's easier to manage.
 * Although it's limiting, for most users 256 characters should be well beyond
 * reasonable.
 */
#define LUA_REPL_BUFSIZE    (256)

/**
 * III:24.3.3   The call stack
 * 
 * We can use stack semantics to avoid needing to heap-allocate memory for each
 * and every function invocation. This is the maximum number of ongoing function
 * calls we can handle for now.
 */
#define LUA_MAXFRAMES       (64)

/** 
 * III:15.2.1: The VM's Stack
 * 
 * For now this is a reasonable default to make, as we don't do heap allocations. 
 * However, in the real world, it's fair to assume that there are projects that
 * end up with stack sizes *greater* than 256. So make your call!
 */
#define LUA_MAXSTACK        ((UINT8_MAX + 1) * LUA_MAXFRAMES)
#define LUA_MAXLOCALS       (UINT8_MAX + 1)

/**
 * Most user-facing operations in Lua using double-precision floating point values.
 * Although they take up 64 bits but have slightly less integer range than 64-bit
 * integers, they are still more than adequate for most people's uses.
 */
#define LUA_NUMBER          double
#define LUA_NUMBER_SCAN     "%lf"
#define LUA_NUMBER_FMT      "%.14g"

/* --- MATH CONFIGURATIONS ------------------------------------------------- {{{
 Series of function-like macros so that we can treat primitive operations as if 
 they were function calls. This also helps unify the implementation of the file
 `vm.c:run_bytecode()`. This is because we can pass function-like macros as 
 macro arguments as well. 
 
 NOTE:

 By default, you must explicitly link to the math library (e.g. `libm.so`). */

#define lua_numadd(lhs, rhs)        ((lhs) + (rhs))
#define lua_numsub(lhs, rhs)        ((lhs) - (rhs))
#define lua_nummul(lhs, rhs)        ((lhs) * (rhs))
#define lua_numdiv(lhs, rhs)        ((lhs) / (rhs))
#define lua_nummod(lhs, rhs)        ((lhs) - floor((lhs) / (rhs)) * (rhs))
#define lua_numpow(lhs, rhs)        (pow(lhs, rhs))
#define lua_numunm(val)             (-(val))
#define lua_numeq(lhs, rhs)         ((lhs) == (rhs))
#define lua_numgt(lhs, rhs)         ((lhs) > (rhs))
#define lua_numlt(lhs, rhs)         ((lhs) < (rhs))

/* }}} ---------------------------------------------------------------------- */

#endif /* LUA_CONFIGURATION_H */
