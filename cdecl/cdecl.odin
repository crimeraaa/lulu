package cdecl

import "core:io"
import "core:os"
import "core:fmt"
import "core:strings"

global_builder: strings.Builder

main :: proc() {
    // We have a leak but I don't really care
    strings.builder_init(&global_builder, len = 0, cap = 256)

    switch len(os.args) {
    case 1: run_interactive()
    case 2: run(os.args[1])
    case:   fmt.eprintln("Usage: %s [decl]\n\t%s",
                         os.args[0], "If no arguments, runs interactively.")
    }
}

run_interactive :: proc() {
    stdin := os.stream_from_handle(os.stdin)
    buf: [256]byte
    for {
        fmt.print(">>> ")
        n_read, err := io.read(stdin, buf[:])
        if err != nil {
            if err != .EOF {
                fmt.eprintln("Error:", err)
            } else {
                fmt.println()
            }
            break
        }
        line := strings.trim_space(string(buf[:n_read]))
        run(line)
    }
}


run :: proc(line: string) {
    strings.builder_reset(&global_builder)
    parser := parser_make(line, strings.to_writer(&global_builder))
    if parser_parse(&parser) {
        parser_fini(&parser)
        fmt.println(strings.to_string(global_builder))
    } else {
        fmt.println("Error:", strings.to_string(global_builder))
    }
}
