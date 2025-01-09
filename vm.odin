#+private
package lulu

import "core:fmt"
import "core:mem"

STACK_MAX :: 256

VM :: struct {
    stack:      [STACK_MAX]Value,
    top, base:  [^]Value,   // 'top' always points to the first free slot.
    chunk:      ^Chunk,
    ip:         [^]Instruction, // Points to next instruction to be executed.
}

Status :: enum u8 {
    Ok,
    Compile_Error,
    Runtime_Error,
}

vm_init :: proc(vm: ^VM) {
    reset_stack(vm)
    vm.chunk = nil
    vm.ip    = nil
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
    vm.ip    = raw_data(chunk.code)
    if !vm_execute(vm) {
        line := chunk.line[ptr_sub(vm.ip, raw_data(chunk.code))]
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
    ip        := vm.ip

    // Needed for error reporting where local 'ip' is long out of scope
    defer vm.ip = ip

    for {
        inst := ip[0]
        defer ip = &ip[1]

        when DEBUG_TRACE_EXEC {
            fmt.printf("      ")
            for value in vm.base[:ptr_sub(vm.top, vm.base)] {
                value_print(value, .Stack)
            }
            fmt.println()
            debug_dump_instruction(chunk^, inst, ptr_sub(ip, raw_data(chunk.code)))
        }

        switch (inst.op) {
        case .Load_Constant:
            a  := inst.a
            bc := inst_get_Bx(inst)
            vm.base[a] = constants[bc]
        case .Load_Nil:
            // Add 1 because we want to include Reg[B]
            for &value in vm.base[inst.a:inst.b + 1] {
                value_set_nil(&value)
            }
        case .Load_Boolean:
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
            start  := inst.a
            n_args := inst.b
            if n_args != 0 {
                vm.top = &vm.base[start + n_args - 1]
            }
            // See: https://www.lua.org/source/5.1/ldo.c.html#luaD_poscall
            stop := int(n_args) - 1 if n_args != 0 else ptr_sub(vm.top, vm.base)
            for arg in vm.base[start:stop] {
                value_print(arg)
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

// `mem.ptr_sub` seems to be off by +1
ptr_sub :: proc(a, b: ^$T) -> int {
    return cast(int)(uintptr(a) - uintptr(b)) / size_of(T)
}
