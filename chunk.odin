#+private
package lulu

MAX_LOCALS :: 200

Chunk :: struct {
    source:          string, // Filename where the chunk originated.
    constants:     [dynamic]Value,
    code:          []Instruction, // len(code) == allocated capacity
    line:          []int, // len(line) == allocated capacity
    pc:              int, // First free index in `code` and `line`.
    stack_used:      int, // Used by VM to determine where stack top should point.

    locals:         [MAX_LOCALS]Local, // 'Declared' local variable stack. See `lparser.h:FuncState::actvar[]`.
    count_local:     int, // How many locals have we registered in total?
}

Local :: struct {
    ident: ^OString,
    depth: int,
    // startpc, endpc: int,
}

chunk_init :: proc(vm: ^VM, chunk: ^Chunk, source: string) {
    chunk.source    = source
    chunk.constants = make([dynamic]Value, vm.allocator)
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
        new_cap  := 8 if n == 0 else n * 2
        chunk.code = make([]Instruction, new_cap, allocator)
        chunk.line = make([]int, new_cap, allocator)
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
chunk_add_constant :: proc(chunk: ^Chunk, value: Value) -> (index: u32) {
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

chunk_destroy :: proc(vm: ^VM, chunk: ^Chunk) {
    allocator := vm.allocator
    delete(chunk.constants)
    delete(chunk.code, allocator)
    delete(chunk.line, allocator)
    chunk.source    = "(freed chunk)"
    chunk.constants = nil
    chunk.code      = nil
    chunk.line      = nil
}
