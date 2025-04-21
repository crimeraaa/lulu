#+private
package lulu

import "core:fmt"

UNINITIALIZED_LOCAL :: -1
INVALID_REG         :: max(u16) // Also applies to locals

Chunk :: struct {
    source:          string, // Filename where the chunk originated.
    locals:        []Local, // 'Declared' local variable stack. See `lparser.h:FuncState::actvar[]`.
    constants:     []Value,
    code:          []Instruction, // len(code) == cap
    line:          []int,         // len(line) == cap
    pc:              int, // First free index in `code` and `line`.
    stack_used:      int, // How many stack slots does this chunk require?
}

Local :: struct {
    ident:         ^OString,
    depth:          int,
    startpc, endpc: int,
}

local_formatter :: proc(fi: ^fmt.Info, arg: any, verb: rune) -> bool {
    local     := (cast(^Local)arg.data)^
    writer    := fi.writer
    n_written := &fi.n
    switch verb {
    case 'v': // verbose; our very own "name mangling"
        n_written^ += fmt.wprintf(writer, "%s__%i(%i:%i)",
            local_to_string(local), local.depth, local.startpc, local.endpc)
        return true
    case 's': // summary
        n_written^ += fmt.wprintf(writer, "%s", local_to_string(local))
        return true
    case:
        return false
    }
}


local_to_string :: proc(local: Local) -> string {
    return ostring_to_string(local.ident)
}

chunk_init :: proc(chunk: ^Chunk, source: string) {
    chunk.source = source
}

chunk_fini :: proc(vm: ^VM, chunk: ^Chunk, compiler: ^Compiler) {
    slice_resize(vm, &chunk.locals,    compiler.count.locals)
    slice_resize(vm, &chunk.constants, compiler.count.constants)
    slice_resize(vm, &chunk.code,      chunk.pc)
    slice_resize(vm, &chunk.line,      chunk.pc)
}

chunk_append :: proc(vm: ^VM, chunk: ^Chunk, inst: Instruction, line: int) -> (pc: int) {
    pc = chunk.pc
    defer chunk.pc += 1
    slice_insert(vm, &chunk.code, pc, inst)
    slice_insert(vm, &chunk.line, pc, line)
    return pc
}

/*
**Overview**
-   Reuse an existing index of a constant value or push a new one.

**Notes**
 -  The index will be needed later on for opcodes to access the value in the
    constants table.
 */
chunk_add_constant :: proc(vm: ^VM, chunk: ^Chunk, value: Value, limit: ^int) -> (index: u32) {
    // Linear search is theoretically very slow!
    for constant, index in chunk.constants[:limit^] {
        if value_eq(constant, value) {
            return cast(u32)index
        }
    }
    defer limit^ += 1
    slice_insert(vm, &chunk.constants, limit^, value)
    return cast(u32)limit^
}


chunk_add_local :: proc(vm: ^VM, chunk: ^Chunk, ident: ^OString, count: ^int) -> (index: u16) {
    defer count^ += 1
    // Don't reserve registers here as our initializer expressions may do so
    // already, or we implicitly load nil.
    slice_insert(vm, &chunk.locals, count^, Local{
        ident   = ident,
        depth   = UNINITIALIZED_LOCAL,
        startpc = 0,
        endpc   = 0,
    })
    return cast(u16)count^
}


chunk_get_local :: proc(chunk: ^Chunk, local_number, pc: int) -> (local: Local, ok: bool) {
    counter := local_number
    for var in chunk.locals {
        /*
        **Notes** (2025-04-21):
        -   This local is alive only beyond this instruction.
        -   We would have reached this point if we have not yet found the local
            already.
        -   This indicates we won't find it anyway.
         */
        if var.startpc > pc {
            break
        }

        /*
        **Notes** (2025-04-21):
        -   This local is definitely active at this instruction.
        -   We try to decrement `counter` the number of times we find a local
            that is definitely active.
         */
        if pc < var.endpc {
            counter -= 1
            // We iterated through this scope the correct number of times, so
            // we have the local we're after.
            if counter == 0 {
                return var, true
            }
        }
    }
    return {}, false
}


chunk_destroy :: proc(vm: ^VM, chunk: ^Chunk) {
    slice_delete(vm, &chunk.locals)
    slice_delete(vm, &chunk.constants)
    slice_delete(vm, &chunk.code)
    slice_delete(vm, &chunk.line)
    chunk.source = "(freed chunk)"
}
