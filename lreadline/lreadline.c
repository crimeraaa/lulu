
#include "lreadline.h"

/* GNU Readline isn't thread-safe anyway */
static lua_State *L2;

/**
 * @note 2025-05-19
 *  Assumes `readline.completer` is definitely a table and currently on top of
 *  the stack.
 *
 * @warning 2025-05-19
 *  This function is very fragile; if Lua throws at any point we will definitely
 *  leak memory.
 *
 * @typedef
 *  `rl_compentry_func_t`
 */
static char *
keyword_generator(const char *line, int state)
{
    /* First call for `line`, `state == 0`. Otherwise, `state != 0`. */
    static size_t line_len;
    static int    list_index, list_len;
    static char   key[2];

    if (state == 0) {
        line_len = strlen(line);
        key[0]   = line[0]; /* At least a nul char */
        key[1]   = '\0';
    }

    /* No text, so insert TAB as-is. */
    if (line_len == 0) {
        rl_insert_text("\t");
        return NULL;
    }

    lua_getfield(L2, -1, key); /* nodes, list=nodes[key]? */
    if (!lua_istable(L2, -1)) {
        lua_pop(L2, 1);
        return NULL;
    }

    if (state == 0) {
        list_index = 1;
        list_len   = (int)lua_objlen(L2, -1); /* list_len = #list */
    }

    for (; list_index <= list_len;) {
        const char *key;
        size_t      key_len;

        lua_rawgeti(L2, -1, list_index++); /* nodes, list, list[list_index] */
        if (!lua_isstring(L2, -1)) {
            lua_pop(L2, 1); /* nodes, list */
            continue;
        }
        key = lua_tolstring(L2, -1, &key_len);
        if (strncmp(line, key, line_len) == 0) {
            /**
             * @brief
             *  It is valid to hold a pointer to a Lua string as long as we
             *  are within C. The moment control is returned back to Lua,
             *  however, we cannot assume the pointer will remain valid.
             *
             * @link
             *  http://lua-users.org/lists/lua-l/2006-02/msg00696.html
             */
            lua_pop(L2, 2); /* nodes */
            return strndup(key, key_len);
        }
        lua_pop(L2, 1); /* nodes, list */
    }
    lua_pop(L2, 1); /* nodes */
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
    char **completions = NULL; /* 1D `char *` array allocated by Readline. */
    (void)start; (void)end;
    rl_attempted_completion_over = 1;
    lua_getglobal(L2, LIBNAME);      /* readline */
    lua_getfield(L2, -1, "completer");  /* readline, nodes=readline.completer */
    if (lua_istable(L2, -1)) {
        completions = rl_completion_matches(line, &keyword_generator);
    }
    lua_pop(L2, 2);
    return completions;
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


/**
 * @brief
 *  A 'completer' is a user-created table which originates from Lua. It
 *  stores what 'words' have been defined so far to use for autocompletion.
 *
 * @details
 *  It must fulfill the following requirements:
 *      1.  It can use single-character strings in the set `[a-zA-Z_]` as keys.
 *          These represent the starting characters of words.
 *
 *      2.  Each character maps to a `string[]` or `nil`. We do not
 *          use a dictionary as that will require an arbitrary iteration.
 *          With arrays (that originate from Lua) we can simply call
 *          `lua_objlen()`.
 */
static int
set_completer(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE); /* t={} */
    lua_getglobal(L, LIBNAME);        /* t, rl */
    lua_pushvalue(L, -2);             /* t, rl, t */
    lua_setfield(L, -2, "completer"); /* t, rl ; rl.completer = t */
    lua_pop(L, 1);                    /* t */
    return 1;
}

static luaL_Reg fns[] = {
    {"readline",      &gnu_readline},
    {"add_history",   &gnu_add_history},
    {"clear_history", &gnu_clear_history},
    {"set_completer", &set_completer},
    {NULL, NULL}
};

LUALIB_API int
luaopen_readline(lua_State *L)
{
    L2 = L;
    rl_attempted_completion_function = keyword_completion;

    /**
     * @note 2025-05-18
     *  'Function `luaL_openlib` was replaced by `luaL_register`.
     *
     * @link
     *  https://www.lua.org/manual/5.1/manual.html#7.3
     */
    luaL_register(L, LIBNAME, fns); /* readline */
    return 1;
}
