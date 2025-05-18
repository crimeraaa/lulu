#ifndef LREADLINE_H
#define LREADLINE_H

#include <readline/readline.h>
#include <readline/history.h>

/* This is specific to this project! */
#include "../lua/src/lua.h"
#include "../lua/src/lauxlib.h"

typedef enum {
    NODE_BASIC,
    NODE_TABLE,
    NODE_FUNCTION,
} Node_Type;

typedef struct Node Node;
struct Node {
    Node      *prev;
    size_t     len;
    Node_Type  type;
    char       data[1];
};

typedef struct {
    Node *upper['Z' - 'A' + 1];
    Node *lower['z' - 'a' + 1];
    Node *underscore;
} Env;

LUALIB_API int
luaopen_readline(lua_State *L);

#endif /* LREADLINE_H */
