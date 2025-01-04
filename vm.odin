#+private
package lulu

import "core:fmt"

STACK_MAX :: 256

VM :: struct {
    stack:  [STACK_MAX]Value,
    sp, bp: [^]Value,
    chunk:  ^Chunk,
    ip:     ^Instruction,
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
    // vm.chunk = chunk
    // vm.ip    = raw_data(chunk.code)
    // return vm_execute(vm)
    compiler_compile(vm, input, name)
    return .Ok
}

vm_execute :: proc(vm: ^VM) -> (status: Status) {
    chunk     := vm.chunk
    code      := chunk.code[:]
    constants := chunk.constants[:]
    ip        := vm.ip

    for {
        inst := ip^
        defer ip = ptr_add(ip, 1)

        when LULU_DEBUG {
            fmt.printf("      ")
            for value in vm.stack[:ptr_sub(vm.sp, vm.bp)] {
                value_print(value, .Stack)
            }
            fmt.println()
            debug_disasm_inst(chunk^, inst, ptr_sub(ip, raw_data(code)))
        }

        switch (inst.op) {
        case .Constant:
            a  := inst.a
            bc := inst_get_uBC(inst)
            vm.bp[a] = constants[bc]
            vm.sp = ptr_add(vm.sp, 1)
        case .Add: binary_op(vm, '+', inst, constants)
        case .Sub: binary_op(vm, '-', inst, constants)
        case .Mul: binary_op(vm, '*', inst, constants)
        case .Div: binary_op(vm, '/', inst, constants)
        case .Unm: vm.bp[inst.a] = -vm.bp[inst.b]
        case .Return:
            a := inst.a
            b := inst.b
            n_results := int(a) + int(b)
            for i in int(a)..<n_results {
                value_print(vm.bp[i])
            }
            // How to properly set stack and base pointers here?
            vm.sp = vm.bp
            return .Ok
        }
    }
}

@(private="file")
binary_op :: proc(vm: ^VM, $op: rune, inst: Instruction, kst: []Value) {
    if cast(int)inst.a == ptr_sub(vm.sp, vm.bp) {
        vm.sp = ptr_add(vm.sp, 1)
    }
    x, y := get_rkb(vm, inst, kst), get_rkc(vm, inst, kst)
    switch op {
    case '+': vm.bp[inst.a] = x + y
    case '-': vm.bp[inst.a] = x - y
    case '*': vm.bp[inst.a] = x * y
    case '/': vm.bp[inst.a] = x / y
    case:
        unreachable()
    }
}

@(private="file")
get_rkb :: #force_inline proc(vm: ^VM, inst: Instruction, constants: []Value)-> Value {
    return constants[rk_get_k(inst.b)] if rk_is_k(inst.b) else vm.bp[inst.b]
}

@(private="file")
get_rkc :: #force_inline proc(vm: ^VM, inst: Instruction, constants: []Value) -> Value {
    return constants[rk_get_k(inst.c)] if rk_is_k(inst.c) else vm.bp[inst.c]
}

@(private="file")
reset_stack :: proc(vm: ^VM) {
    vm.bp = &vm.stack[0]
    vm.sp = vm.bp
}

@(private="file")
ptr_add :: proc "contextless" (a: ^$T, #any_int offset: int) -> ^T {
    return cast(^T)(uintptr(a) + uintptr(offset*size_of(T)))
}

@(private="file")
ptr_sub :: proc {
    ptr_sub_ptr,
    ptr_sub_int,
}

// 'mem.ptr_sub' seems to give the wrong result...
@(private="file")
ptr_sub_ptr :: proc "contextless" (a, b: ^$T) -> int {
    return int(uintptr(a) - uintptr(b)) / size_of(T)
}

@(private="file")
ptr_sub_int :: proc "contextless" (a: ^$T, #any_int offset: int) -> ^T {
    return cast(^T)(uintptr(a) - uintptr(offset*size_of(T)))
}
