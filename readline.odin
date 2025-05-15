#+private
package lulu_main

@require import "core:c/libc"
@require import "core:strings"
@require import "core:os"
@require import "core:io"

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

read_line :: proc(buffer: []byte) -> (line: string, ok: bool) {
    s := readline(PROMPT)
    // Minor QOL; users rarely want to jump back to empty lines!
    if line, ok = string(s), s != nil; ok && line != "" {
        add_history(s)
    }
    return line, ok
}

/*
**Notes**
-   Assumes `line` was allocated via `read_line()`.
-   If not, then you're gonna have a bad time!
 */
free_line :: proc(line: string) {
    libc.free(cast(rawptr)strings.unsafe_string_to_cstring(line))
}

free_all_lines :: proc() {
    clear_history()
}


} else /* ODIN_OS != .Linux */ {


read_line :: proc(buffer: []byte) -> (line: string, ok: bool) {
    os.write_string(os.stdout, PROMPT)
    stdin       := io.to_reader(os.stream_from_handle(os.stdin))
    n_read, err := io.read(stdin, buffer)
    return string(buffer[:n_read]), err == nil
}

// Nothing to do, just keep around for consistency.
free_line :: proc(line: string) {}
free_all_lines :: proc() {}

}
