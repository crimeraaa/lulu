#define _GNU_SOURCE
#include <stdlib.h> /* malloc */
#include <string.h> /* strlen, _POSIX_C_SOURCE: strdup */
#include <stdio.h>  /* _GNU_SOURCE: asprintf */

#include "lreadline.h"

enum {
    /* keywords */
    KW_AND, KW_BREAK, KW_DO, KW_ELSE, KW_ELSEIF, KW_END, KW_FALSE, KW_FOR,
    KW_FUNCTION, KW_IF, KW_IN, KW_LOCAL, KW_RETURN, KW_REPEAT, KW_THEN, KW_TRUE,
    KW_UNTIL, KW_WHILE,

    /* base functions */
    FN_ASSERT, FN_COLLECTGARBAGE, FN_DOFILE, FN_ERROR, FN_GETFENV,
    FN_GETMETATABLE, FN_IPAIRS, FN_LOAD, FN_LOADFILE, FN_LOADSTRING, FN_MODULE,
    FN_NEXT, FN_PAIRS, FN_PCALL, FN_PRINT, FN_RAWEQUAL, FN_RAWGET, FN_RAWSET,
    FN_REQUIRE, FN_SELECT, FN_SETFENV, FN_SETMETATABLE, FN_TONUMBER,
    FN_TOSTRING, FN_TYPE, FN_UNPACK, FN_XPCALL,

    /* base libraries */
    LIB_COROUTINE, LIB_DEBUG, LIB_IO, LIB_MATH, LIB_OS, LIB_PACKAGE,
    LIB_STRING, LIB_TABLE,
};

#define RESERVED_FIRST  KW_AND
#define RESERVED_LAST   KW_WHILE

#define BASEFN_FIRST    FN_ASSERT
#define BASEFN_LAST     FN_UNPACK

#define BASELIB_FIRST   LIB_COROUTINE
#define BASELIB_LAST    LIB_TABLE

#define IS_RESERVED(i)  (RESERVED_FIRST <= (i) && (i) <= RESERVED_LAST)
#define IS_BASEFN(i)    (BASEFN_FIRST   <= (i) && (i) <= BASEFN_LAST)
#define IS_BASELIB(i)   (BASELIB_FIRST  <= (i) && (i) <= BASELIB_LAST)

/**
 * @typedef
 *  `rl_compentry_func_t`
 */
static char *
keyword_generator(const char *line, int state)
{
    /**
     * @note 2025-05-18
     *  -   This is very ugly and error prone
     *
     * @link https://www.lua.org/manual/5.1/
     */
    static const char *const reserved[] = {
        /* keywords */
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "if", "in", "local", "return", "repeat", "then", "true",
        "until", "while",

        /* base functions */
        "assert", "collectgarbage", "dofile", "error", "getfenv",
        "getmetatable", "ipairs", "load", "loadfile", "loadstring", "module",
        "next", "pairs", "pcall", "print", "rawequal", "rawget", "rawset",
        "require", "select", "setfenv", "setmetatable", "tonumber", "tostring",
        "type", "unpack", "xpcall",

        /* base libraries */
        "coroutine", "debug", "io", "math", "os", "package", "string", "table",
        NULL
    };

    /* First call for `line`, `state == 0`. Otherwise, `state != 0`. */
    static int    list_index;
    static size_t line_len;

    if (state == 0) {
        list_index = 0;
        line_len   = strlen(line);
    }

    /* No text, so insert TAB as-is. */
    if (line_len == 0) {
        rl_insert_text("\t");
        return NULL;
    }

    for (;;) {
        const int   index = list_index++;
        const char *name  = reserved[index];
        if (name == NULL) {
            break;
        }
        if (strncmp(line, name, line_len) == 0) {
            char *out = strdup(name);
            if (IS_RESERVED(index)) {
                rl_completion_append_character = ' ';
            } else if (IS_BASEFN(index)) {
                rl_completion_append_character = '(';
            } else if (IS_BASELIB(index)) {
                rl_completion_append_character = '.';
            }
            return out;
        }
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

LUALIB_API int
luaopen_readline(lua_State *L)
{
    rl_attempted_completion_function = keyword_completion;

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
