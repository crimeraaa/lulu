#+private
package main

@require import c "core:c/libc"
@require import "core:os"
@require import "core:io"

import "lulu"
import rl "readline"

when USE_READLINE {

setup_readline :: proc(vm: ^lulu.VM) {
    rl.bind_key('\t', rl.insert)
}

cleanup_readline :: proc() {
    rl.clear_history()
}

read_line :: proc(vm: ^lulu.VM) -> (line: string, ok: bool) {
    s := rl.readline(PROMPT)
    if s == nil {
        return
    }
    defer c.free(rawptr(s))

    tmp := string(s)

    // Minor QOL; users rarely want to jump back to empty lines!
    if tmp != "" {
        rl.add_history(s)
    }
    return check_line(vm, tmp), true
}

} else /* ODIN_OS != .Linux */ {


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
