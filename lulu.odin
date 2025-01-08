package lulu

import "core:fmt"
import "core:io"
import "core:log"
import "core:os"

_ :: log

LULU_DEBUG         :: #config(LULU_DEBUG, ODIN_DEBUG)
DEBUG_TRACE_EXEC   :: LULU_DEBUG
DEBUG_PRINT_CODE   :: LULU_DEBUG

@(private="file")
g_vm := &VM{}

main :: proc() {
    when LULU_DEBUG {
        logger_opts := log.Options{.Level, .Short_File_Path, .Line, .Procedure, .Terminal_Color}
        logger := log.create_console_logger(opt = logger_opts)
        context.logger = logger
        defer log.destroy_console_logger(logger)
    }

    vm_init(g_vm)
    defer vm_destroy(g_vm)

    switch len(os.args) {
    case 1: repl(g_vm)
    case 2: run_file(g_vm, os.args[1])
    case:   fmt.eprintfln("Usage: %s [script]", os.args[0])
    }
}

repl :: proc(vm: ^VM) {
    buf: [256]byte

    // Use 'io' so we can use 'io.read' to check for '.Eof'
    stdin := io.to_reader(os.stream_from_handle(os.stdin))
    for {
        fmt.print(">>> ")
        n_read, io_err := io.read(stdin, buf[:])
        if io_err != nil {
            // <C-D> (Unix) or <C-Z><CR> (Windows) is expected.
            if io_err != .EOF {
                fmt.println("Error: ", io_err)
            } else {
                fmt.println()
            }
            break
        }
        vm_interpret(vm, string(buf[:n_read]), "stdin")
    }
}

run_file :: proc(vm: ^VM, file_name: string) {
    data, ok := os.read_entire_file(file_name)
    if !ok {
        fmt.eprintfln("Failed to read file %q.", file_name)
        return
    }
    defer delete(data)

    vm_interpret(vm, string(data[:]), file_name)

}
