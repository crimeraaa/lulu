package lulu

import "core:c/libc"
import "core:fmt"
import "core:log"
import "core:os"
import "core:mem"

_ :: log
_ :: mem

LULU_DEBUG                  :: #config(LULU_DEBUG, ODIN_DEBUG)
DEBUG_TRACE_EXEC            :: LULU_DEBUG
DEBUG_PRINT_CODE            :: LULU_DEBUG
CONSTANT_FOLDING_ENABLED    :: #config(CONSTANT_FOLDING_ENABLED, !LULU_DEBUG)

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
}

@(private="file")
global_vm := &VM{}

main :: proc() {
    when LULU_DEBUG {
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

    vm_init(global_vm, context.allocator)
    defer vm_destroy(global_vm)

    switch len(os.args) {
    case 1: repl(global_vm)
    case 2: run_file(global_vm, os.args[1])
    case:   fmt.eprintfln("Usage: %s [script]", os.args[0])
    }
}

@(private="file")
repl :: proc(vm: ^VM) {
    for {
        input := readline(">>> ")
        if input == nil {
            break
        }
        defer libc.free(cast(rawptr)input)
        add_history(input)
        vm_interpret(vm, string(input), "stdin")
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
    vm_interpret(vm, string(data[:]), file_name)

}
