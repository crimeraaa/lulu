package lulu

import "core:fmt"
import "core:log"
import "core:os"
import "core:mem"

_ :: log
_ :: mem

DEBUG :: #config(DEBUG, ODIN_DEBUG)

// Debug Info
DEBUG_TRACE_EXEC :: DEBUG
DEBUG_PRINT_CODE :: DEBUG

// Runtime Features
USE_CONSTANT_FOLDING :: #config(USE_CONSTANT_FOLDING, !DEBUG)
USE_READLINE :: #config(USE_READLINE, ODIN_OS == .Linux)

PROMPT :: ">>> "

main :: proc() {
    when DEBUG {
        logger_opts :: log.Options{.Level, .Short_File_Path, .Line, .Procedure, .Terminal_Color}
        logger := log.create_console_logger(opt = logger_opts)
        defer log.destroy_console_logger(logger)
        context.logger = logger

        track: mem.Tracking_Allocator
        mem.tracking_allocator_init(&track, context.allocator)
        context.allocator = mem.tracking_allocator(&track)
        defer {
            defer mem.tracking_allocator_destroy(&track)

            if n := len(track.allocation_map); n > 0 {
                fmt.eprintfln("=== %v allocations not freed: ===", n)
                for _, entry in track.allocation_map {
                    fmt.eprintfln("- %v bytes @ %v", entry.size, entry.location)
                }
            }
            if n := len(track.bad_free_array); n > 0 {
                fmt.eprintfln("=== %v incorrect frees: ===", n)
                for entry in track.bad_free_array {
                    fmt.eprintfln("- %p @ %v", entry.memory, entry.location)
                }
            }
        }
    }

    vm := open()
    defer close(vm)

    switch len(os.args) {
    case 1: run_interactive(vm)
    case 2: run_file(vm, os.args[1])
    case:   fmt.eprintfln("Usage: %s [script]", os.args[0])
    }
}

@(private="file")
run_interactive :: proc(vm: ^VM) {
    for {
        input := read_line() or_break
        defer free_line(input)
        // Interpret even if empty, this will return 0 registers.
        run_input(vm, input, "stdin")
    }
}

@(private="file")
run_file :: proc(vm: ^VM, file_name: string) {
    data, ok := os.read_entire_file(file_name)
    if !ok {
        fmt.eprintfln("Failed to read file %q.", file_name)
        return
    }
    defer delete(data)
    run_input(vm, string(data), file_name)
}

@(private="file")
run_input :: proc(vm: ^VM, input, source: string) {
    if vm_interpret(vm, input, source) != .Ok {
        err_msg, _ := to_string(vm, -1)
        fmt.eprintln(err_msg)
    }
}
