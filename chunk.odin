#+private
package lulu

import "core:mem"

Chunk :: struct {
    source      : string, // Filename where the chunk originated.
    constants   : [dynamic]Value,
    code        : [dynamic]Instruction,
    line        : [dynamic]int,
}

chunk_init :: proc(chunk: ^Chunk, source: string, allocator: mem.Allocator) {
    chunk.source    = source
    chunk.constants = make([dynamic]Value, allocator)
    chunk.code      = make([dynamic]Instruction, allocator)
    chunk.line      = make([dynamic]int, allocator)
}

chunk_append :: proc(chunk: ^Chunk, inst: Instruction, line: int) -> (pc: int) {
    append(&chunk.code, inst)
    append(&chunk.line, line)
    return len(chunk.code) - 1
}

/*
Note:
 -  The index will be needed later on for opcodes to access the value in the
    constants table.
 */
chunk_add_constant :: proc(chunk: ^Chunk, value: Value) -> u32 {
    constants := &chunk.constants
    // Linear search is theoretically very slow!
    for constant, index in constants {
        if value_eq(constant, value) {
            return u32(index)
        }
    }
    append(constants, value)
    return cast(u32)len(constants) - 1
}

chunk_destroy :: proc(chunk: ^Chunk) {
    delete(chunk.constants)
    delete(chunk.code)
    delete(chunk.line)
    chunk.source    = "(freed chunk)"
    chunk.constants = nil
    chunk.code      = nil
    chunk.line      = nil
}
