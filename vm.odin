#+private
package lulu

import "core:fmt"
import "core:mem"

STACK_MAX :: 256

VM :: struct {
    stack:      [STACK_MAX]Value,
    top, base:  [^]Value,   // 'top' always points to the first free slot.
    chunk:      ^Chunk,
    ip:         ^Instruction,
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
    reset_stack(vm)
    vm.chunk = nil
    vm.ip    = nil
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
    return vm_execute(vm)
}

// Analogous to 'vm.c:run()' in the book.
vm_execute :: proc(vm: ^VM) -> (status: Status) {
    chunk     := vm.chunk
    code      := chunk.code[:]
    constants := chunk.constants[:]
    ip        := vm.ip

    for {
        inst := ip^
        defer ip = ptr_add(ip, 1)

        when DEBUG_TRACE_EXEC {
            fmt.printf("      ")
            for value in vm.stack[:ptr_sub(vm.top, vm.base)] {
                value_print(value, .Stack)
            }
            fmt.println()
            debug_disasm_inst(chunk^, inst, ptr_sub(ip, raw_data(code)))
        }

        switch (inst.op) {
        case .Constant:
            a  := inst.a
            bc := inst_get_Bx(inst)
            vm.base[a] = constants[bc]
        case .Add: binary_op(vm, number_add, inst, constants)
        case .Sub: binary_op(vm, number_sub, inst, constants)
        case .Mul: binary_op(vm, number_mul, inst, constants)
        case .Div: binary_op(vm, number_div, inst, constants)
        case .Mod: binary_op(vm, number_mod, inst, constants)
        case .Pow: binary_op(vm, number_pow, inst, constants)
        case .Unm: vm.base[inst.a] = -vm.base[inst.b]
        case .Return:
            a := inst.a // Location of register A
            b := inst.b // #results
            if b != 0 {
                vm.top = ptr_add(&vm.base[a], b - 1)
            }
            // See: https://www.lua.org/source/5.1/ldo.c.html#luaD_poscall
            n_results := int(b) - 1 if b != 0 else ptr_sub(vm.top, vm.base)
            for i in int(a)..<n_results {
                value_print(vm.base[i])
            }
            return .Ok
        }
    }
}

@(private="file")
binary_op :: proc(vm: ^VM, $op: proc(x, y: Value) -> Value, inst: Instruction, kst: []Value) {
    x, y := get_RK(vm, inst.b, kst), get_RK(vm, inst.c, kst)
    vm.base[inst.a] = op(x, y)
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

ptr_add :: proc(a: ^$T, #any_int offset: int) -> ^T {
    return cast(^T)(uintptr(a) + uintptr(offset*size_of(T)))
}

ptr_sub :: proc {
    ptr_sub_ptr,
    ptr_sub_int,
}

// 'mem.ptr_sub' seems to give the wrong result...
ptr_sub_ptr :: proc(a, b: ^$T) -> int {
    return int(uintptr(a) - uintptr(b)) / size_of(T)
}

ptr_sub_int :: proc(a: ^$T, #any_int offset: int) -> ^T {
    return cast(^T)(uintptr(a) - uintptr(offset*size_of(T)))
}
