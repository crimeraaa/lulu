#+private
package lulu

import "core:fmt"
import "core:mem"

STACK_MAX :: 256

VM :: struct {
    stack:      [STACK_MAX]Value,
    top, base:  [^]Value,   // 'top' always points to the first free slot.
    chunk:      ^Chunk,
    pc:         int, // Index of the next instruction to be executed.
}

Status :: enum u8 {
    Ok,
    Compile_Error,
    Runtime_Error,
}

vm_init :: proc(vm: ^VM) {
    reset_stack(vm)
    vm.chunk = nil
    vm.pc    = 0
}

vm_destroy :: proc(vm: ^VM) {
    vm_init(vm)
}

vm_interpret :: proc(vm: ^VM, input, name: string) -> (status: Status) {
    chunk := &Chunk{}
    chunk_init(chunk, name)
    defer chunk_destroy(chunk)

    if !compiler_compile(vm, chunk, input) {
        return .Compile_Error
    }
    vm.chunk = chunk
    vm.pc    = 0
    if !vm_execute(vm) {
        line := chunk.line[vm.pc]
        fmt.eprintfln("%s:%i: %s", chunk.source, line, "Bad expression somewhere!")
        reset_stack(vm)
        return .Runtime_Error
    }
    return .Ok
}

// Analogous to 'vm.c:run()' in the book.
vm_execute :: proc(vm: ^VM) -> (ok: bool) {
    chunk     := vm.chunk
    code      := chunk.code[:]
    constants := chunk.constants[:]
    pc        := vm.pc
    
    // Needed for error reporting where local 'pc' is long out of scope
    defer vm.pc = pc

    for {
        inst := code[pc]
        defer pc += 1

        when DEBUG_TRACE_EXEC {
            fmt.printf("      ")
            for value := vm.base; value < vm.top; value = &value[1] {
                value_print(value[0], .Stack)
            }
            fmt.println()
            debug_disasm_inst(chunk^, inst, pc)
        }

        switch (inst.op) {
        case .Constant:
            a  := inst.a
            bc := inst_get_Bx(inst)
            vm.base[a] = constants[bc]
        case .Nil:
            a, b := inst.a, inst.b
            for i in a..=b {
                value_set_nil(&vm.base[i])
            }
        case .Boolean:
            value_set_boolean(&vm.base[inst.a], inst.b == 1)
        case .Add: binary_op(vm, number_add, inst, constants) or_return
        case .Sub: binary_op(vm, number_sub, inst, constants) or_return
        case .Mul: binary_op(vm, number_mul, inst, constants) or_return
        case .Div: binary_op(vm, number_div, inst, constants) or_return
        case .Mod: binary_op(vm, number_mod, inst, constants) or_return
        case .Pow: binary_op(vm, number_pow, inst, constants) or_return
        case .Unm:
            operand := get_RK(vm, inst.b, constants)
            if !value_is_number(operand) {
                return false
            }
            value_set_number(&vm.base[inst.a], number_unm(operand.data.number))
        case .Return:
            a := inst.a // Location of register A
            b := inst.b // #results
            if b != 0 {
                vm.top = &vm.base[a + b - 1]
            }
            // See: https://www.lua.org/source/5.1/ldo.c.html#luaD_poscall
            n_results := int(b) - 1 if b != 0 else mem.ptr_sub(vm.top, vm.base)
            for i in int(a)..<n_results {
                value_print(vm.base[i])
            }
            return true
        }
    }
}

@(private="file")
binary_op :: proc(vm: ^VM, $op: proc(x, y: f64) -> f64, inst: Instruction, kst: []Value) -> (ok: bool) {
    x, y := get_RK(vm, inst.b, kst), get_RK(vm, inst.c, kst)
    if !value_is_number(x) || !value_is_number(y) {
        return false
    }
    value_set_number(&vm.base[inst.a], op(x.data.number, y.data.number))
    return true
}

@(private="file")
get_RK :: proc(vm: ^VM, b_or_c: u16, constants: []Value) -> Value {
    return constants[rk_get_k(b_or_c)] if rk_is_k(b_or_c) else vm.base[b_or_c]
}

@(private="file")
reset_stack :: proc(vm: ^VM) {
    vm.base = &vm.stack[0]
    vm.top  = vm.base
}
