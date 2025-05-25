/*
**Links**
-   https://tiswww.case.edu/php/chet/readline/readline.html
 */
package readline

import c "core:c/libc"


// https://odin-lang.org/docs/overview/#foreign-system
@(extra_linker_flags="-lreadline")
foreign import gnu_readline "system:readline"

@(extra_linker_flags="-lhistory")
foreign import gnu_history "system:history"

#assert(size_of(b32) == size_of(c.int))

command_func_t    :: #type proc "c" (count, key: c.int) -> b32
compentry_func_t  :: #type proc "c" (text: cstring, state: b32) -> cstring
completion_func_t :: #type proc "c" (text: cstring, start, end: c.int) -> [^]cstring

foreign gnu_readline {
    // https://www.lua.org/source/5.1/luaconf.h.html#lua_readline
    readline :: proc "c" (prompt: cstring = nil) -> cstring ---
}

/*
**Links**
-   https://git.savannah.gnu.org/cgit/readline.git/tree/text.c
 */
@(link_prefix="rl_")
foreign gnu_readline {
    bind_key           :: proc "c" (key: c.int, function: command_func_t) -> c.int ---
    insert             :: proc "c" (count, key: c.int) -> b32 ---
    insert_text        :: proc "c" (text: cstring) -> (written: c.int) ---
    completion_matches :: proc "c" (text: cstring, function: compentry_func_t) -> [^]cstring ---

    attempted_completion_function: completion_func_t
    attempted_completion_over:     b32
}

// See your readline installation, e.g. in `usr/include/readline/history.h`
foreign gnu_history {
    add_history    :: proc "c" (text: cstring) ---

    // `offset` is `-1` if `text` was not found and nothing was changed.
    history_search :: proc "c" (text: cstring, direction: c.int) -> (offset: c.int) ---
    clear_history  :: proc "c" () ---
}
