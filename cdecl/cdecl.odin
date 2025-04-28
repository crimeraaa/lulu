package cdecl

import "core:io"
import "core:os"
import "core:fmt"
import "core:strings"

main :: proc() {
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
    stdout := os.stream_from_handle(os.stdout)
    tokens := tokenize(line)
    defer delete(tokens)
    
    parser := parser_make(tokens, stdout)
    decl   := decl_make()
    defer decl_destroy(&decl)

    if parser_parse(&parser, &decl) {
        parser_dump(&parser)
    }
    fmt.println()
}
