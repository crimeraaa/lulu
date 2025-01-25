#+private
package lulu

import "core:c/libc"
import "core:strings"
import "core:os"
import "core:io"

when USE_READLINE {

_ :: io
_ :: os

// https://odin-lang.org/docs/overview/#foreign-system
@(extra_linker_flags="-lreadline")
foreign import gnu_readline "system:readline"

@(extra_linker_flags="-lhistory")
foreign import gnu_history "system:history"

// https://www.lua.org/source/5.1/luaconf.h.html#lua_readline
foreign gnu_readline {
    @(link_name="readline")
    _readline :: proc "c" (prompt: cstring) -> cstring ---
}

foreign gnu_history {
    @(link_name="add_history")
    _add_history :: proc "c" (buffer: cstring) ---
}

read_line :: proc() -> (line: string, ok: bool) {
    s := _readline(PROMPT)
    // Minor QOL; users rarely want to jump back to empty lines!
    if line, ok = string(s), s != nil; ok && line != "" {
        _add_history(s)
    }
    return line, ok
}

/*
Notes:
-   Assumes `line` was allocated via `read_line()`.
-   If not, then you're gonna have a bad time!
 */
free_line :: proc(line: string) {
    libc.free(cast(rawptr)strings.unsafe_string_to_cstring(line))
}

} else /* ODIN_OS != .Linux */ {

_ :: libc
_ :: strings

// NOTE(2025-01-25): Not thread-safe!
read_line :: proc() -> (line: string, ok: bool) {
    @(static)
    buf: [256]byte
    os.write_string(os.stdout, PROMPT)
    stdin  := io.to_reader(os.stream_from_handle(os.stdin))
    n, err := io.read(stdin, buf[:])
    return string(buf[:n]), err == nil
}

free_line :: proc(line: string) {
    // Nothing to do, just keep around for consistency
}

}
