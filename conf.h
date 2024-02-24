#ifndef LUA_CONFIGURATION_H
#define LUA_CONFIGURATION_H

#ifdef _WIN32
#define LUA_DIRSEP "\\"
#else /* _WIN32 not defined. */
#define LUA_DIRSEP "/"
#endif

#include <math.h>

/**
 * This is the default stack-allocated size of the REPL's char buffer.
 * We prefer stack-allocated over heap-allocated because it's easier to manage.
 * Although it's limiting, for most users 256 characters should be well beyond
 * reasonable.
 */
#define LUA_REPL_BUFSIZE    (256)

/** 
 * III:15.2.1: The VM's Stack
 * 
 * For now this is a reasonable default to make, as we don't do heap allocations. 
 * However, in the real world, it's fair to assume that there are projects that
 * end up with stack sizes *greater* than 256. So make your call!
 */
#define LUA_VM_STACKSIZE    (256)

/**
 * Most user-facing operations in Lua using double-precision floating point values.
 * Although they take up 64 bits but have slightly less integer range than 64-bit
 * integers, they are still more than adequate for most people's uses.
 * 
 * NOTE:
 * 
 * In `vm.c:interpret_vm()`, we use `pow()` and `fmod()`, which are for doubles
 * specifically. You'll need to explicitly link with `libm` or `libmath`.
 */
#define LUA_NUMBER          double
#define LUA_NUMBER_SCAN     "%lf"
#define LUA_NUMBER_FMT      "%.14g"

/**
 * This is a series of function-like macros so that we can treat primitive
 * operations as if they were function calls.
 * 
 * This also helps unify the implementation of `vm.c:run_bytecode()`.
 */
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

#endif /* LUA_CONFIGURATION_H */
