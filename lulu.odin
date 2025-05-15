package lulu_main

import "core:fmt"
import "core:os"
@require import "core:log"
@require import "core:mem"

import "lulu"

USE_READLINE :: #config(USE_READLINE, ODIN_OS == .Linux)

PROMPT :: ">>> "

main :: proc() {
    when ODIN_DEBUG {
        logger_opts :: log.Options{.Level, .Short_File_Path, .Line, .Procedure, .Terminal_Color}
        logger := log.create_console_logger(opt = logger_opts)
        defer log.destroy_console_logger(logger)
        context.logger = logger

        track: mem.Tracking_Allocator
        mem.tracking_allocator_init(&track, context.allocator)
        context.allocator = mem.tracking_allocator(&track)
        defer {
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
            mem.tracking_allocator_destroy(&track)
        }
    }

    run_interactive :: proc(vm: ^lulu.VM) {
        buffer: [256]byte
        defer free_all_lines()

        for {
            input := read_line(buffer[:]) or_break
            need_return := len(input) > 0 && input[0] == '='
            if need_return {
                prev := input
                input = lulu.push_fstring(vm, "return %s", prev[1:])
                free_line(prev)
            }
            // Interpret even if empty, this will return 0 registers.
            run_input(vm, input, "stdin")

            if !need_return {
                free_line(input)
            }
        }
    }

    run_file :: proc(vm: ^lulu.VM, file_name: string) {
        data, ok := os.read_entire_file(file_name)
        if !ok {
            fmt.eprintfln("Failed to read file %q.", file_name)
            return
        }
        run_input(vm, string(data), file_name)
        delete(data)
    }

    run_input :: proc(vm: ^lulu.VM, input, source: string) {
        if lulu.run(vm, input, source) != nil {
            err_msg, _ := lulu.to_string(vm, -1)
            fmt.eprintln(err_msg)
            return
        }

        fmt.printf("returned %i values: ", lulu.get_top(vm))
        for i in 1..=lulu.get_top(vm) {
            if i != 1 {
                fmt.print(", ")
            }
            fmt.print(lulu.to_string(vm, i))
        }
        fmt.println()
    }

    vm, err := lulu.open()
    if err != nil {
        fmt.eprintfln("Failed to open lulu; %t %v", err)
        return
    }
    defer lulu.close(vm)

    switch len(os.args) {
    case 1: run_interactive(vm)
    case 2: run_file(vm, os.args[1])
    case:   fmt.eprintfln("Usage: %s [script]", os.args[0])
    }
}
