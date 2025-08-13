#+private
package main

@require import c "core:c/libc"
@require import "core:os"
@require import "core:io"

import "lulu"

when USE_READLINE {

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

setup_readline :: proc(vm: ^lulu.VM) {
    bind_key('\t', insert)
}

cleanup_readline :: proc() {
    clear_history()
}

read_line :: proc(vm: ^lulu.VM) -> (line: string, ok: bool) {
    s := readline(PROMPT)
    if s == nil {
        return
    }
    defer c.free(rawptr(s))

    tmp := string(s)

    // Minor QOL; users rarely want to jump back to empty lines!
    if tmp != "" {
        add_history(s)
    }
    return check_line(vm, tmp), true
}

} else /* !USE_READLINE */ {


read_line :: proc(vm: ^lulu.VM) -> (line: string, ok: bool) {
    buf: [256]byte
    os.write_string(os.stdout, PROMPT)
    stdin := io.to_reader(os.stream_from_handle(os.stdin))
    n_read, err := io.read(stdin, buf[:])
    if err != nil {
        return
    }
    return check_line(vm, string(buf[:n_read])), true
}

}


/*
**Overview**
-   For interactive mode, we want to intern `line` by pushing it to the stack.
-   Later on, if we want to have interactive line continuation, we can utilize
    this behavior.

**Notes** (2025-05-16)
-   Like in Lua, interactive mode also has a unique behavior: if the first
    character is '=', then the comma-separated list of expressions following it
    are returned.
 */
check_line :: proc(vm: ^lulu.VM, line: string) -> string {
    if len(line) > 0 && line[0] == '=' {
        return lulu.push_fstring(vm, "return %s", line[1:])
    } else {
        return lulu.push_string(vm, line)
    }
}
