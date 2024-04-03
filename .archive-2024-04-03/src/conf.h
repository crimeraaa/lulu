/* See: https://www.lua.org/source/5.1/luaconf.h.html */
#ifndef LUA_CONFIGURATION_H
#define LUA_CONFIGURATION_H

#include <limits.h>
#include <stddef.h>
#include <stdlib.h> /* malloc family */
#include <setjmp.h> /* jmpbuf_t, setjmp, longjmp */
#include <stdio.h>  /* printf family */
#include <string.h>
#include <math.h>   /* Needed by some `luai_*` macros, link with `-lm`. */

#if defined(_WIN32)
#define LUA_DIRSEP "\\"
#else /* _WIN32 not defined. */
#define LUA_DIRSEP "/"
#endif /* _WIN32 */

/* Should only be defined when building .dll for Windows targets. */
#if defined(LUA_BUILD_AS_DLL)

/* Primarily an MSVC feature but MinGW should support this as well. */
#if defined(LUA_CORE) || defined(LUA_LIB)
#define LUA_API __declspec(dllexport)
#else /* neither LUA_CORE nor LUA_LIB defined. */
#define LUA_API __declspec(dllimport)
#endif /* LUA_CORE || LUA_LIB */

#else /* LUA_BUILD_AS_DLL not defined. */

#define LUA_API         extern

#endif /* LUA_BUILD_AS_DLL */

#define LUALIB_API      LUA_API
#define LUA_PROMPT      "> "

/* Stack-allocated buffer length for the REPL of the interpreter. */
#define LUA_MAXINPUT    512

#if defined(DEBUG_USE_ASSERT)
#include <assert.h>
/**
 * @brief   macro to assert the truthiness of expressions. Acts as sanity check.
 *          Define `DEBUG_USE_ASSERT` when building the interpreter to enable.
 *
 * @note    The actual Lua source code uses a macro `LUA_USE_APICHECK` instead.
 *          See:
 *          - https://www.lua.org/source/5.1/luaconf.h.html#luai_apicheck
 */
#define luai_apicheck(vm, expr)     { (void)(vm); assert(expr); }
#else /* DEBUG_USE_ASSERT not defined. */
#define luai_apicheck(vm, expr)     { (void)(vm); }
#endif /* DEBUG_USE_ASSERT */

#define LUAI_MAXLOCALS      200
#define LUAI_MAXUPVALUES    60
#define LUAI_BUFFERSIZE     BUFSIZ

#define LUA_NUMBER          double
#define LUA_NUMBER_SCAN     "%lf"
#define LUA_NUMBER_FMT      "%.14g"
/**
 * @brief   Fixed buffer size when converting non-string Lua values to strings.
 *          Should fit `"function: %p"` and `LUA_NUMBER_FMT`.
 *
 * @note    If your machine's pointers are printed out in a much longer format
 *          or your desired precision is large, change this value as needed.
 */
#define LUA_MAXTOSTRING     64
#define lua_num2str(s,n)    snprintf((s), LUA_MAXTOSTRING, LUA_NUMBER_FMT, (n))
#define lua_str2num(s,p)    strtod((s), (p))

#define luai_numadd(x,y)    ((x) + (y))
#define luai_numsub(x,y)    ((x) - (y))
#define luai_nummul(x,y)    ((x) * (y))
#define luai_numdiv(x,y)    ((x) / (y))
#define luai_nummod(x,y)    ((x) - floor((x)/(y)) * (y))
#define luai_numpow(x,y)    (pow(x,y))
#define luai_numunm(x)      (-(x))
#define luai_numeq(x,y)     ((x) == (y))
#define luai_numlt(x,y)     ((x) < (y))
#define luai_numle(x,y)     ((x) <= (y))
#define luai_numisnan(x)    (!luai_numeq(x,x))

#endif /* LUA_CONFIGURATION_H */
