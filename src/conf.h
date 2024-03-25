/* See: https://www.lua.org/source/5.1/luaconf.h.html */
#ifndef LUA_CONFIGURATION_H
#define LUA_CONFIGURATION_H

#include <limits.h>
#include <stddef.h>
#include <stdlib.h> /* malloc family */
#include <stdint.h> 
#include <stdio.h>  /* printf family */
#include <string.h>
#include <math.h>   /* Needed by some `luai_*` macros, link with `-lm`. */

#if defined(_WIN32)
#define LUA_DIRSEP          "\\"
#else /* _WIN32 not defined. */
#define LUA_DIRSEP          "/"
#endif /* _WIN32 */

/* Should only be defined when building .dll for Windows targets. */
#if defined(LUA_BUILD_AS_DLL)

/* Primarily an MSVC feature but MinGW should support this as well. */
#if defined(LUA_CORE) || defined(LUA_LIB)
#define LUA_API             __declspec(dllexport)
#else /* neither LUA_CORE nor LUA_LIB defined. */
#define LUA_API             __declspec(dllimport)
#endif /* LUA_CORE || LUA_LIB */

#else /* LUA_BUILD_AS_DLL not defined. */

#define LUA_API             extern

#endif /* LUA_BUILD_AS_DLL */

#define LUALIB_API          LUA_API
#define LUA_PROMPT          "> "

/* Stack-allocated buffer length for the REPL of the interpreter. */
#define LUA_MAXINPUT        512

/* Try to detect if target machine has 16 bit integers or 32 bit `int`. 
Subtract 20 to avoid overflows in the comparison. */
#if (INT_MAX - 20) < 32760
#define LUA_BITSINT         16
#elif INT_MAX > 2147483640L
#define LUAI_BITSINT        32
#else /* INT_MAX is greater than (2^31)-1 or 32-bit signed integer limit */
#error "Please define LUA_BITSINT with the exact number of bits in an `int`."
#endif /* INT_MAX comparisons for LUA_BITSINT */

#if LUAI_BITSINT >= 32
#define LUAI_UINT32         unsigned int
#define LUAI_INT32          int
#define LUAI_MAXINT32       INT_MAX
#define LUAI_UMEM           size_t 
#define LUAI_MEM            ptrdiff_t
#else /* LUA_BITSINT < 32, likely 16-bit ints */
#define LUAI_UINT32         unsigned long
#define LUAI_INT32          long
#define LUAI_MAXINT32       LONG_MAX
#define LUAI_UMEM           unsigned long
#define LUAI_MEM            long
#endif /* LUA_BITSINT */

#define LUAI_MAXLOCALS      200
#define LUAI_MAXUPVALUES    60
#define LUAI_BUFFERSIZE     BUFSIZ

#define LUA_NUMBER          double
#define LUA_NUMBER_SCAN     "%lf"
#define LUA_NUMBER_FMT      "%.14g"
#define lua_num2str(s,n)    sprintf((s), LUA_NUMBER_FMT, (n))
#define lua_str2num(s,p)    strtod((s), (p))

#define luai_numadd(x,y)    ((x)+(y))
#define luai_numsub(x,y)    ((x)-(y))
#define luai_nummul(x,y)    ((x)*(y))
#define luai_numdiv(x,y)    ((x)/(y))
#define luai_nummod(x,y)    ((x) - floor((x)/(y)) * (y))
#define luai_numpow(x,y)    (pow(x,y))
#define luai_numunm(x)      (-(a))
#define luai_numeq(x,y)     ((x)==(y))
#define luai_numlt(x,y)     ((x)<(y))
#define luai_numle(x,y)     ((x)<=(y))
#define luai_numisnan(x)    (!luai_numeq(x,x))

#endif /* LUA_CONFIGURATION_H */
