#+private
package lulu_main

@require import "core:c/libc"
@require import "core:strings"
@require import "core:os"
@require import "core:io"

import "lulu"

when USE_READLINE {

// https://odin-lang.org/docs/overview/#foreign-system
@(extra_linker_flags="-lreadline")
foreign import gnu_readline "system:readline"

@(extra_linker_flags="-lhistory")
foreign import gnu_history "system:history"

// https://www.lua.org/source/5.1/luaconf.h.html#lua_readline
foreign gnu_readline {
    readline :: proc "c" (prompt: cstring) -> cstring ---
}

foreign gnu_history {
    add_history :: proc "c" (buffer: cstring) ---
    clear_history :: proc "c" () ---
}

read_line :: proc(vm: ^lulu.VM) -> (line: string, ok: bool) {
    s := readline(PROMPT)
    defer libc.free(rawptr(s))
    // Minor QOL; users rarely want to jump back to empty lines!

    if ok = (s != nil); ok {
        tmp := string(s)
        if tmp != "" {
            add_history(s)
        }
        line = check_line(vm, tmp)
    }
    return line, ok
}

clear_lines :: proc() {
    clear_history()
}


} else /* ODIN_OS != .Linux */ {


read_line :: proc(vm: ^lulu.VM) -> (line: string, ok: bool) {
    buf: [256]byte
    os.write_string(os.stdout, PROMPT)
    stdin := io.to_reader(os.stream_from_handle(os.stdin))
    n_read, err := io.read(stdin, buf[:])

    if ok = err != nil; ok {
        line = check_line(vm, string(buf[:n_read]))
    }
    return line, ok
}

// Nothing to do, just keep around for consistency.
clear_lines :: proc() {}

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
