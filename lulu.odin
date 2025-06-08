package main

import "core:fmt"
import "core:os"
@require import "core:mem"

import "lulu"

USE_READLINE :: #config(LULU_USE_READLINE, ODIN_OS == .Linux)

PROMPT :: ">>> "

main :: proc() {
    when ODIN_DEBUG {
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
        when USE_READLINE {
            setup_readline(vm)
            defer cleanup_readline()
        }

        for {
            input := read_line(vm) or_break
            // Interpret even if empty, this will return 0 registers.
            run_input(vm, input, "stdin")
            // TODO(2025-05-16): Handle incomplete lines?
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
        if err := lulu.run(vm, input, source); err != .Ok {
            err_msg := lulu.to_string(vm, -1)
            fmt.eprintfln("[%w] %s", err, err_msg)
            return
        }

        fmt.printf("returned %i values: ", lulu.get_top(vm))
        for i in 1..=lulu.get_top(vm) {
            if i != 1 {
                fmt.print(", ")
            }
            switch lulu.type(vm, i) {
            case .None:     fmt.print("no value") // Impossible
            case .Nil:      fmt.print("nil")
            case .Boolean:  fmt.print(lulu.to_boolean(vm, i))
            case .Number:   fmt.print(lulu.to_number(vm, i))
            case .String:   fmt.print(lulu.to_string(vm, i))
            case .Table, .Function:
                fmt.printf("%s: %p", lulu.type_name(vm, i), lulu.to_pointer(vm, i))
            }
        }
        fmt.println()
    }

    vm, err := lulu.open()
    if err != nil {
        fmt.eprintfln("Failed to open lulu; %w", err)
        return
    }
    lulu.open_base(vm)
    defer lulu.close(vm)

    switch len(os.args) {
    case 1: run_interactive(vm)
    case 2: run_file(vm, os.args[1])
    case:   fmt.eprintfln("Usage: %s [script]", os.args[0])
    }
}
