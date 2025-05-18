/*
**Links**
-   https://tiswww.case.edu/php/chet/readline/readline.html
 */
package readline

import "core:c/libc"


// https://odin-lang.org/docs/overview/#foreign-system
@(extra_linker_flags="-lreadline")
foreign import gnu_readline "system:readline"

@(extra_linker_flags="-lhistory")
foreign import gnu_history "system:history"

command_func_t    :: #type proc "c" (count, key: libc.int) -> libc.int
completion_func_t :: #type proc "c" (text: cstring, start, end: libc.int) -> [^]cstring
compentry_func_t  :: #type proc "c" (text: cstring, state: b32) -> cstring

foreign gnu_readline {
    // https://www.lua.org/source/5.1/luaconf.h.html#lua_readline
    readline :: proc "c" (prompt: cstring = nil) -> cstring ---
}

@(link_prefix="rl_")
foreign gnu_readline {
    bind_key           :: proc "c" (key: libc.int, function: command_func_t) -> libc.int ---
    insert             :: proc "c" (count, key: libc.int) -> libc.int ---
    completion_matches :: proc "c" (text: cstring, function: compentry_func_t) -> [^]cstring ---

    attempted_completion_function: completion_func_t
    attempted_completion_over:     b32
}

foreign gnu_history {
    add_history   :: proc "c" (text: cstring) ---
    clear_history :: proc "c" () ---
}
