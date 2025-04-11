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
        if value_eq(constant, value) do return cast(u32)index
    }
    append(constants, value)
    return cast(u32)len(constants) - 1
}


chunk_add_local :: proc(chunk: ^Chunk, ident: ^OString) -> (index: u16, ok: bool) {
    if chunk.count_local >= len(chunk.locals) do return INVALID_REG, false

    // Don't reserve registers here as our initializer expressions may do so
    // already, or we implicitly load nil.
    defer chunk.count_local += 1
    chunk.locals[chunk.count_local] = {ident, UNINITIALIZED_LOCAL}
    return cast(u16)chunk.count_local, true
}


/*
Notes:
-   By itself, `chunk` does not know anything about how many local variables are
    actually active (i.e. in scope). Thus you have to pass it explicitly.
 */
chunk_resolve_local :: proc(chunk: ^Chunk, ident: ^OString, count_active: int) -> (index: u16, ok: bool) {
    // Reverse because top most are the more recent locals.
    #reverse for local, index in chunk.locals[:count_active] {
        // If uninitialized, skip to allow: `x = 1; local x = x`
        if local.ident == ident && local.depth != UNINITIALIZED_LOCAL {
            return cast(u16)index, true
        }
    }
    return INVALID_REG, false
}


/*
Notes:
-   We allow shadowing across different scopes, so the `depth` parameter is
    mainly meant to communicate the caller's desired scope depth.
 */
chunk_check_shadowing :: proc(chunk: ^Chunk, ident: ^OString, depth: int) -> (ok: bool) {
    for local, index in chunk.locals[:chunk.count_local] {
        // We hit an initialized local in an outer scope?
        if local.depth != UNINITIALIZED_LOCAL && local.depth < depth do break
        if local.ident == ident do return true
    }
    return false
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
