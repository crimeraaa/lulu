#+private
package lulu

Chunk :: struct {
    source:    string, // Filename where the chunk originated.
    constants: [dynamic]Value,
    code:      [dynamic]Instruction,
    line:      [dynamic]int,
}

chunk_init :: proc(chunk: ^Chunk, source: string, allocator := context.allocator) {
    chunk.source    = source
    chunk.constants = make([dynamic]Value, allocator)
    chunk.code      = make([dynamic]Instruction, allocator)
    chunk.line      = make([dynamic]int, allocator)
}

chunk_append :: proc(chunk: ^Chunk, inst: Instruction, line: int) {
    append(&chunk.code, inst)
    append(&chunk.line, line)
}

/* 
Note:
 -  The index will be needed later on for opcodes to access the value in the
    constants table.
 */
chunk_add_constant :: proc(chunk: ^Chunk, value: Value) -> u32 {
    // Linear search is theoretically very slow, but we don't have any other choice!
    for i in 0..<len(chunk.constants) {
        if value_eq(chunk.constants[i], value) {
            return u32(i)
        }
    }
    append(&chunk.constants, value)
    return cast(u32)len(chunk.constants) - 1
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
