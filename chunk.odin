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
    constants:       DyArray(Value),
    code:            DyArray(Instruction),
    line:            DyArray(int),
    pc:              int, // First free index in `code` and `line`.
    stack_used:      int, // Used by VM to determine where stack top should point.

    locals:          DyArray(Local), // 'Declared' local variable stack. See `lparser.h:FuncState::actvar[]`.
}

Local :: struct {
    ident: ^OString `fmt:"q"`, // see `string.odin:ostring_fmt()`.
    depth: int,
    // startpc, endpc: int,
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
    dyarray_append(vm, &chunk.code, inst)
    dyarray_append(vm, &chunk.line, line)
    defer chunk.pc += 1
    return chunk.pc
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
    dyarray_append(vm, &chunk.locals, Local{ident, UNINITIALIZED_LOCAL})
    return cast(u16)dyarray_len(chunk.locals) - 1
}


chunk_destroy :: proc(vm: ^VM, chunk: ^Chunk) {
    allocator := vm.allocator
    dyarray_delete(vm, &chunk.constants)
    dyarray_delete(vm, &chunk.code)
    dyarray_delete(vm, &chunk.line)
    dyarray_delete(vm, &chunk.locals)
    chunk.source = "(freed chunk)"
}
