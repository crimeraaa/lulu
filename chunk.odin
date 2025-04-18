#+private
package lulu

import "core:math"
import "core:fmt"
import "core:io"
import "core:mem"

MAX_LOCALS          :: 200
UNINITIALIZED_LOCAL :: -1
INVALID_REG         :: max(u16) // Also applies to locals

Chunk :: struct {
    source:          string, // Filename where the chunk originated.
    constants:     []Value,
    count_constants: int,
    code:          []Instruction, // len(code) == allocated capacity
    line:          []int, // len(line) == allocated capacity
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
        n_written^ += fmt.wprintf(writer, "{{%q, %i}}", local.ident, local.depth)
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
    allocator := vm.allocator
    if n := len(chunk.code); pc >= n {
        old_code := chunk.code
        old_line := chunk.line
        defer {
            delete(old_code, allocator)
            delete(old_line, allocator)
        }
        mem_err: mem.Allocator_Error
        new_cap  := 8 if n == 0 else n * 2
        chunk.code, mem_err = make([]Instruction, new_cap, allocator)
        if mem_err != nil {
            vm_memory_error(vm)
        }

        chunk.line, mem_err = make([]int, new_cap, allocator)
        if mem_err != nil {
            vm_memory_error(vm)
        }

        copy(chunk.code, old_code)
        copy(chunk.line, old_line)
    }
    chunk.code[pc] = inst
    chunk.line[pc] = line
    chunk.pc += 1
    return pc
}

/*
Note:
 -  The index will be needed later on for opcodes to access the value in the
    constants table.
 */
chunk_add_constant :: proc(vm: ^VM, chunk: ^Chunk, value: Value) -> (index: u32) {
    // Linear search is theoretically very slow!
    for constant, index in chunk.constants {
        if value_eq(constant, value) {
            return cast(u32)index
        }
    }

    defer chunk.count_constants += 1
    count := chunk.count_constants

    // Need to resize before appending?
    if count >= len(chunk.constants) {
        new_cap        := max(8, math.next_power_of_two(count + 1))
        new_array, err := make([]Value, new_cap, vm.allocator)
        old_array      := chunk.constants
        defer delete(old_array)
        if err != nil {
            vm_memory_error(vm)
        }
        copy(new_array, chunk.constants)
        chunk.constants = new_array
    }
    chunk.constants[count] = value
    return cast(u32)count
}


chunk_add_local :: proc(vm: ^VM, chunk: ^Chunk, ident: ^OString) -> (index: u16) {
    // Don't reserve registers here as our initializer expressions may do so
    // already, or we implicitly load nil.
    dyarray_append(vm, &chunk.locals, Local{ident, UNINITIALIZED_LOCAL})
    return cast(u16)dyarray_len(chunk.locals) - 1
}


chunk_destroy :: proc(vm: ^VM, chunk: ^Chunk) {
    allocator := vm.allocator
    delete(chunk.constants)
    delete(chunk.code, allocator)
    delete(chunk.line, allocator)
    dyarray_delete(vm, &chunk.locals)
    chunk.source    = "(freed chunk)"
    chunk.constants = nil
    chunk.code      = nil
    chunk.line      = nil
}
