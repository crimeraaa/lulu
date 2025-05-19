#ifndef LREADLINE_H
#define LREADLINE_H


#define _GNU_SOURCE
#include <stdlib.h> /* malloc */
#include <string.h> /* strlen, _POSIX_C_SOURCE: strdup */

#include <readline/readline.h>
#include <readline/history.h>

/* This is specific to this project! */
#include "../lua/src/lua.h"
#include "../lua/src/lauxlib.h"

#define LIBNAME     "readline"

LUALIB_API int
luaopen_readline(lua_State *L);

#endif /* LREADLINE_H */
