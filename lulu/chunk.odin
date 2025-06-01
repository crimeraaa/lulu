#+private
package lulu

import "core:fmt"

NO_REG :: max(u16) // Also applies to locals

Chunk :: struct {
    source:          string, // Filename where the chunk originated.
    locals:        []Local, // 'Declared' local variable stack. See `lparser.h:FuncState::actvar[]`.
    constants:     []Value,
    code:          []Instruction, // len(code) == cap
    line:          []int,         // len(line) == cap
    stack_used:      int, // How many stack slots does this chunk require?
    is_vararg:       bool,
}

Local :: struct {
    ident:         ^OString,
    startpc, endpc: int,
}

local_formatter :: proc(fi: ^fmt.Info, arg: any, verb: rune) -> bool {
    var   := (cast(^Local)arg.data)^
    ident := local_to_string(var)
    switch verb {
    case 'v': // verbose
        fi.n += fmt.wprintf(fi.writer, "%s@code[%i:%i]", ident, var.startpc,
                            var.endpc)
    case 's': // summary
        fi.n += fmt.wprintf(fi.writer, "%s$%i", ident, var.startpc)
    case:
        return false
    }
    return true
}


local_to_string :: proc(var: Local) -> string {
    return ostring_to_string(var.ident)
}

chunk_init :: proc(c: ^Chunk, source: string) {
    c.source     = source
    c.stack_used = 2 // Ensure both R(0) and R(1) can always be loaded
}

chunk_fini :: proc(vm: ^VM, ch: ^Chunk, cl: ^Compiler) {
    slice_resize(vm, &ch.locals,    cl.count.locals)
    slice_resize(vm, &ch.constants, cl.count.constants)
    slice_resize(vm, &ch.code,      cl.pc)
    slice_resize(vm, &ch.line,      cl.pc)
}

chunk_append :: proc(vm: ^VM, c: ^Chunk, i: Instruction, line: int, pc: ^int) {
    slice_insert(vm, &c.code, pc^, i)
    slice_insert(vm, &c.line, pc^, line)
    pc^ += 1
}

/*
**Overview**
-   Reuse an existing index of a constant value or push a new one.

**Notes**
 -  The index will be needed later on for opcodes to access the value in the
    constants table.
 */
chunk_add_constant :: proc(vm: ^VM, c: ^Chunk, v: Value, limit: ^int) -> (index: u32) {
    // Linear search is theoretically very slow!
    n := limit^
    for vk, i in c.constants[:n] {
        if value_eq(vk, v) {
            return u32(i)
        }
    }
    slice_insert(vm, &c.constants, n, v)
    limit^ += 1
    return u32(n)
}


chunk_add_local :: proc(vm: ^VM, c: ^Chunk, ident: ^OString, count: ^int) -> (index: u16) {
    defer count^ += 1
    // Don't reserve registers here as our initializer expressions may do so
    // already, or we implicitly load nil.
    slice_insert(vm, &c.locals, count^, Local{
        ident   = ident,
        startpc = 0,
        endpc   = 0,
    })
    return cast(u16)count^
}


chunk_get_local :: proc(c: ^Chunk, local_number, pc: int) -> (local: Local, ok: bool) {
    counter := local_number
    for var in c.locals {
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


chunk_destroy :: proc(vm: ^VM, c: ^Chunk) {
    slice_delete(vm, &c.locals)
    slice_delete(vm, &c.constants)
    slice_delete(vm, &c.code)
    slice_delete(vm, &c.line)
    c.source = "(freed chunk)"
}
