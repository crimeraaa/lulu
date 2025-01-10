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
    ra: ^Value

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
        
        // Most instructions use this!
        ra = &vm.base[inst.a]

        switch (inst.op) {
        case .Load_Constant:
            bc := inst_get_Bx(inst)
            ra^ = constants[bc]
        case .Load_Nil:
            // Add 1 because we want to include Reg[B]
            for &value in vm.base[inst.a:inst.b + 1] {
                value_set_nil(&value)
            }
        case .Load_Boolean: value_set_boolean(ra, inst.b == 1)
        case .Add: arith_op(vm, number_add, ra, inst, constants) or_return
        case .Sub: arith_op(vm, number_sub, ra, inst, constants) or_return
        case .Mul: arith_op(vm, number_mul, ra, inst, constants) or_return
        case .Div: arith_op(vm, number_div, ra, inst, constants) or_return
        case .Mod: arith_op(vm, number_mod, ra, inst, constants) or_return
        case .Pow: arith_op(vm, number_pow, ra, inst, constants) or_return
        case .Unm:
            rb := vm.base[inst.b]
            if !value_is_number(rb) {
                return false
            }
            value_set_number(ra, number_unm(rb.data.number))
        case .Eq, .Neq:
            rb, rc := get_RK(vm, inst.b, constants), get_RK(vm, inst.c, constants)
            b := value_eq(rb, rc)
            value_set_boolean(ra, b if inst.op == .Eq else !b)
        case .Lt:   compare_op(vm, number_lt, ra, inst, constants) or_return
        case .Gt:   compare_op(vm, number_gt, ra, inst, constants) or_return
        case .Leq:  compare_op(vm, number_leq, ra, inst, constants) or_return
        case .Geq:  compare_op(vm, number_geq, ra, inst, constants) or_return
        case .Not:
            x := get_RK(vm, inst.b, constants)
            value_set_boolean(ra, value_is_falsy(x))
        case .Return:
            start := inst.a
            // If vararg, keep top as-is
            if n_results := inst.b; n_results != 0 {
                vm.top = mem.ptr_offset(ra, n_results - 1)
            }
            // See: https://www.lua.org/source/5.1/ldo.c.html#luaD_poscall
            for arg in vm.base[start:ptr_sub(vm.top, vm.base)] {
                value_print(arg)
            }
            return true
        }
    }
}

// Rough analog to C macro
@(private="file")
arith_op :: #force_inline proc(vm: ^VM, $op: proc(x, y: f64) -> f64, ra: ^Value, inst: Instruction, constants: []Value) -> (ok: bool) {
    x, y := get_RK(vm, inst.b, constants), get_RK(vm, inst.c, constants)
    if !value_is_number(x) || !value_is_number(y) {
        return false
    }
    value_set_number(ra, op(x.data.number, y.data.number))
    return true
}

@(private="file")
compare_op :: #force_inline proc(vm: ^VM, $op: proc(x, y: f64) -> bool, ra: ^Value, inst: Instruction, constants: []Value) -> (ok: bool) {
    x, y := get_RK(vm, inst.b, constants), get_RK(vm, inst.c, constants)
    if !value_is_number(x) || !value_is_number(y) {
        return false
    }
    value_set_boolean(ra, op(x.data.number, y.data.number))
    return true
}

@(private="file")
get_RK :: #force_inline proc(vm: ^VM, b_or_c: u16, constants: []Value) -> Value {
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
