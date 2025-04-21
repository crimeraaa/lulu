#+private
package lulu

import "core:math"
import "core:fmt"
import "core:mem"

MAX_LOCALS          :: 200
UNINITIALIZED_LOCAL :: -1
INVALID_REG         :: max(u16) // Also applies to locals

Chunk :: struct {
    source:          string, // Filename where the chunk originated.
    locals:          DyArray(Local), // 'Declared' local variable stack. See `lparser.h:FuncState::actvar[]`.
    constants:       DyArray(Value),
    code:          []Instruction, // len(code) == cap
    line:          []int,         // len(line) == cap
    pc:              int, // First free index in `code` and `line`.
    stack_used:      int, // How many stack slots does this chunk require?
}

Local :: struct {
    ident: ^OString `fmt:"q"`, // see `string.odin:ostring_fmt()`.
    depth: int,
    startpc, endpc: int,
}

local_formatter :: proc(fi: ^fmt.Info, arg: any, verb: rune) -> bool {
    local     := (cast(^Local)arg.data)^
    writer    := fi.writer
    n_written := &fi.n
    switch verb {
    case 'v':
        n_written^ += fmt.wprintf(writer, "%s(%i)", local.ident, local.depth)
        return true
    case:
        return false
    }
}

chunk_init :: proc(vm: ^VM, chunk: ^Chunk, source: string) {
    chunk.source = source
}

chunk_append :: proc(vm: ^VM, chunk: ^Chunk, inst: Instruction, line: int) -> (pc: int) {
    pc = chunk.pc
    defer chunk.pc += 1
    slice_insert(vm, &chunk.code, pc, inst)
    slice_insert(vm, &chunk.line, pc, line)
    return pc
}

/*
Note:
 -  The index will be needed later on for opcodes to access the value in the
    constants table.
 */
chunk_add_constant :: proc(vm: ^VM, chunk: ^Chunk, value: Value) -> (index: u32) {
    // Linear search is theoretically very slow!
    for constant, index in dyarray_slice(&chunk.constants) {
        if value_eq(constant, value) {
            return cast(u32)index
        }
    }
    dyarray_append(vm, &chunk.constants, value)
    return cast(u32)dyarray_len(chunk.constants) - 1
}


chunk_add_local :: proc(vm: ^VM, chunk: ^Chunk, ident: ^OString) -> (index: u16) {
    // Don't reserve registers here as our initializer expressions may do so
    // already, or we implicitly load nil.
    dyarray_append(vm, &chunk.locals, Local{
        ident   = ident,
        depth   = UNINITIALIZED_LOCAL,
        startpc = chunk.pc,
        endpc   = 0,
    })
    return cast(u16)dyarray_len(chunk.locals) - 1
}


chunk_get_local_name :: proc(chunk: ^Chunk, reg, pc: int) -> (name: string, ok: bool) {
    counter := reg
    locals  := dyarray_slice(&chunk.locals)
    for local in locals {
        // This local is outside the range of the pc's scope.
        if local.startpc > pc {
            break;
        }
        // This local is in range of the pc's scope?
        if pc < local.endpc {
            counter -= 1
            if counter == 0 {
                return ostring_to_string(local.ident), true
            }
        }
    }
    return "", false
}


chunk_destroy :: proc(vm: ^VM, chunk: ^Chunk) {
    dyarray_delete(vm, &chunk.locals)
    dyarray_delete(vm, &chunk.constants)
    slice_delete(vm, &chunk.code)
    slice_delete(vm, &chunk.line)
    chunk.source = "(freed chunk)"
}
