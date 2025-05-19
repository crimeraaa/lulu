#ifndef LREADLINE_H
#define LREADLINE_H

#include <readline/readline.h>
#include <readline/history.h>

/* This is specific to this project! */
#include "../lua/src/lua.h"
#include "../lua/src/lauxlib.h"

LUALIB_API int
luaopen_readline(lua_State *L);

#endif /* LREADLINE_H */
