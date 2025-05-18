#define _GNU_SOURCE
#include <stdlib.h> /* malloc */
#include <string.h> /* strlen, _POSIX_C_SOURCE: strdup */
#include <stdio.h>  /* _GNU_SOURCE: asprintf */

#include "lreadline.h"

static Env env;

static bool
is_lower(char ch, int *i)
{
    bool ok = 'a' <= ch && ch <= 'z';
    if (ok) {
        *i = ch - 'a';
    }
    return ok;
}

static bool
is_upper(char ch, int *i)
{
    bool ok = 'A' <= ch && ch <= 'Z';
    if (ok) {
        *i = ch - 'A';
    }
    return ok;
}

static Node *
find_node(const char *line, size_t len, Node *node)
{
    while (node != NULL) {
        /* Compare the prefix, not the entire string! */
        if (strncmp(line, node->data, len) == 0) {
            return node;
        }
        node = node->prev;
    }
    return NULL;
}

static Node **
parent_node(const char *line, size_t len)
{
    int i;
    if (len == 0) {
        return NULL;
    }
    if (is_upper(line[0], &i)) {
        return &env.upper[i];
    } else if (is_lower(line[0], &i)) {
        return &env.lower[i];
    } else if (line[0] == '_') {
        return &env.underscore;
    }
    return NULL;
}

static Node *
first_node(const char *line, size_t len)
{
    Node *node = *parent_node(line, len);
    return find_node(line, len, node);
}


/**
 * @typedef
 *  `rl_compentry_func_t`
 */
static char *
keyword_generator(const char *line, int state)
{
    /* First call for `line`, `state == 0`. Otherwise, `state != 0`. */
    static size_t      len;
    static const Node *node;

    if (state == 0) {
        len = strlen(line);
    }

    /* No text, so insert TAB as-is. */
    if (len == 0) {
        rl_insert_text("\t");
        return NULL;
    }

    /*  In order to check for *multiple* completions, we need to use shared
        state between function calls. */
    if (state == 0) {
        node = first_node(line, len);
    } else {
        node = find_node(line, len, node->prev);
    }

    if (node != NULL) {
        switch (node->type) {
        case NODE_BASIC:    rl_completion_append_character = ' '; break;
        case NODE_TABLE:    rl_completion_append_character = '.'; break;
        case NODE_FUNCTION: rl_completion_append_character = '('; break;
        }
        return strndup(node->data, node->len);
    }

    return NULL; /* No possible completions. */
}

/**
 * @typedef
 *  `rl_completion_func_t`
 *
 * @link
 *  https://thoughtbot.com/blog/tab-completion-in-gnu-readline
 */
static char **
keyword_completion(const char *line, int start, int end)
{
    (void)start; (void)end;
    rl_attempted_completion_over = 1;
    return rl_completion_matches(line, &keyword_generator);
}

static int
gnu_readline(lua_State *L)
{
    const char *prompt;
    char       *line;

    /**
     * @brief man 3 readline
     *  'If `prompt` is `NULL` or the empty string, no prompt is issued.'
     */
    prompt = luaL_optstring(L, 1, NULL);

    /**
     * @brief man 3 readline
     *  'The line returned is allocated with `malloc(3)`; the caller must free
     *  it when finished. The line has the final newline removed, so only the
     *  text of the line remains.'
     */
    line = readline(prompt);

    /**
     * @brief man 3 readline
     *  'If EOF is encountered while reading a line, and the line is empty,
     *  `NULL` is returned. If an EOF is read with a non-empty line, it is
     *  treated as a newline.'
     */
    if (line == NULL) {
        lua_pushnil(L);
    } else {
        size_t len = strlen(line);
        if (len > 0) {
            add_history(line);
        }
        lua_pushlstring(L, line, len);
        free(line);
    }
    return 1;
}

static int
gnu_add_history(lua_State *L)
{
    size_t len;
    const char *line = luaL_checklstring(L, 1, &len);
    if (len > 0) {
        add_history(line);
    }
    return 0;
}

static int
gnu_clear_history(lua_State *L)
{
    (void)L;
    clear_history();
    return 0;
}

static luaL_Reg fns[] = {
    {"readline",      &gnu_readline},
    {"add_history",   &gnu_add_history},
    {"clear_history", &gnu_clear_history},
    {NULL, NULL}
};

static void
add_node(Node_Type type, const char *key, size_t len)
{
    Node  *node;
    Node **list = parent_node(key, len);

    /* At least 1 char is allowed, not accounting for padding. */
    node = malloc(sizeof(*node) + len);
    node->prev      = *list;
    node->len       = len;
    node->type      = type;
    node->data[len] = '\0';
    memcpy(node->data, key, len);
    *list = node;
}


/**
 * @note 2025-05-18
 *  -   This is very ugly and error prone
 *
 * @link https://www.lua.org/manual/5.1/
 */
static const char *const reserved[] = {
    /* keywords */
    "and", "break", "do", "else", "elseif", "end", "false", "for",
    "function", "if", "in", "local", "nil", "not", "or", "return", "repeat",
    "then", "true", "until", "while"
};


LUALIB_API int
luaopen_readline(lua_State *L)
{
    int i;
    rl_attempted_completion_function = keyword_completion;

    lua_getglobal(L, "_G"); /* _G */
    lua_pushnil(L);         /* _G, k */
    while (lua_next(L, -2) != 0) /* _G, k, _G[k] */ {
        const char *key;
        size_t      len;
        Node_Type   type;

        /* Can't complete non-string keys */
        if (!lua_isstring(L, -2)) {
            continue;
        }

        switch (lua_type(L, -1)) {
        case LUA_TTABLE:    type = NODE_TABLE;    break;
        case LUA_TFUNCTION: type = NODE_FUNCTION; break;
        default:            type = NODE_BASIC;    break;
        }
        key = lua_tolstring(L, -2, &len);
        add_node(type, key, len);
        lua_pop(L, 1); /* _G, k */
    }

    for (i = 0; i < (int)(sizeof(reserved) / sizeof(reserved[0])); ++i) {
        const char *key = reserved[i];
        add_node(NODE_BASIC, key, strlen(key));
    }

    /**
     * @note 2025-05-18
     *  'Function `luaL_openlib` was replaced by `luaL_register`.
     *
     * @link
     *  https://www.lua.org/manual/5.1/manual.html#7.3
     */
    luaL_register(L, "readline", fns);
    return 1;
}
