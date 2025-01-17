#+private
package lulu

import "core:c/libc"
import "core:fmt"
import "core:mem"
import "core:strings"

STACK_MAX :: 256

VM :: struct {
    stack       : [STACK_MAX]Value,
    allocator   : mem.Allocator,
    builder     : strings.Builder,
    interned    : Table,
    globals     : Table,
    objects     : ^Object_Header,
    top, base   : [^]Value,   // 'top' always points to the first free slot.
    chunk       : ^Chunk,
    ip          : [^]Instruction, // Points to next instruction to be executed.
    handlers    : ^Error_Handler,
}

Error_Handler :: struct {
    prev: ^Error_Handler,
    buffer: libc.jmp_buf,
    status: Status,
}

Status :: enum u8 {
    Ok,
    Compile_Error,
    Runtime_Error,
}

vm_error :: proc(vm: ^VM, status: Status, source: string, line: int, format: string, args: ..any) {
    handler := vm.handlers
    assert(handler != nil, "what do you want to happen?")
    fmt.eprintf("%s:%i: ", source, line)
    fmt.eprintfln(format, ..args)
    handler.status = status
    libc.longjmp(&handler.buffer, 1)
}

vm_runtime_error :: proc(vm: ^VM, $format: string, args: ..any) {
    chunk := vm.chunk
    line := chunk.line[ptr_sub(vm.ip, raw_data(chunk.code))]
    vm_error(vm, .Runtime_Error, chunk.source, line, "Attempt to " + format, ..args)
}

vm_init :: proc(vm: ^VM, allocator: mem.Allocator) {
    reset_stack(vm)

    table_init(&vm.interned, allocator)
    vm.interned.type = .Table
    vm.interned.prev = nil

    // _G is not part of the collectable objects list.
    table_init(&vm.globals, allocator)
    vm.globals.type = .Table
    vm.globals.prev = nil

    vm.builder   = strings.builder_make(allocator)
    vm.allocator = allocator
    vm.chunk     = nil
    vm.ip        = nil
}

vm_destroy :: proc(vm: ^VM) {
    object_free_all(vm)

    reset_stack(vm)
    table_destroy(&vm.interned)
    table_destroy(&vm.globals)
    strings.builder_destroy(&vm.builder)
    vm.objects  = nil
    vm.chunk    = nil
    vm.ip       = nil

}

vm_interpret :: proc(vm: ^VM, input, name: string) -> (status: Status) {
    chunk := &Chunk{}
    chunk_init(chunk, name, vm.allocator)
    defer chunk_destroy(chunk)

    vm.handlers = &Error_Handler{}
    defer vm.handlers = nil

    if libc.setjmp(&vm.handlers.buffer) != 0 {
        return vm.handlers.status
    }
    compiler_compile(vm, chunk, input)
    vm.chunk = chunk
    vm.ip    = raw_data(chunk.code)
    vm_execute(vm)
    // if run_err := vm_execute(vm); run_err != nil {
        // line := chunk.line[ptr_sub(vm.ip, raw_data(chunk.code))]
        // fmt.eprintf("%s:%i: Attempt to %s ", chunk.source, line, runtime_error_strings[run_err])
        // #partial switch run_err {
        // case .Arith, .Compare, .Concat:
        //     rk_b, rk_c := get_rk_bc(vm, vm.ip[-1], vm.chunk.constants[:])
        //     fmt.eprintfln("a %s value and %s",
        //                    value_type_name(rk_b), value_type_name(rk_c))
        // case .Undefined_Global:
        //     key := chunk.constants[inst_get_Bx(vm.ip[-1])]
        //     assert(value_is_string(key))
        //     fmt.eprintfln("%q", ostring_to_string(key.ostring))
        // case: unreachable()
        // }
        // reset_stack(vm)
        // return .Runtime_Error
    // }
    return .Ok
}

// Analogous to 'vm.c:run()' in the book.
vm_execute :: proc(vm: ^VM) {
    chunk     := vm.chunk
    constants := chunk.constants[:]
    globals   := &vm.globals
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
        case .Get_Global:
            key := constants[inst_get_Bx(inst)]
            value, ok := table_get(globals, key)
            if !ok {
                ident := ostring_to_string(key.ostring)
                vm_runtime_error(vm, "read undefined global '%s'", ident)
            }
            ra^ = value
        case .Set_Global:
            key := constants[inst_get_Bx(inst)]
            table_set(globals, key, ra^)
        case .Print:
            for arg in vm.base[inst.a:inst.b] {
                value_print(arg, .Print)
            }
            fmt.println()
        case .Add: arith_op(vm, number_add, ra, inst, constants)
        case .Sub: arith_op(vm, number_sub, ra, inst, constants)
        case .Mul: arith_op(vm, number_mul, ra, inst, constants)
        case .Div: arith_op(vm, number_div, ra, inst, constants)
        case .Mod: arith_op(vm, number_mod, ra, inst, constants)
        case .Pow: arith_op(vm, number_pow, ra, inst, constants)
        case .Unm:
            rb := vm.base[inst.b]
            if !value_is_number(rb) {
                arith_error(vm, rb, rb)
            }
            value_set_number(ra, number_unm(rb.data.number))
        case .Eq, .Neq:
            rb, rc := get_rk_bc(vm, inst, constants)
            b := value_eq(rb, rc)
            value_set_boolean(ra, b if inst.op == .Eq else !b)
        case .Lt:   compare_op(vm, number_lt, ra, inst, constants)
        case .Gt:   compare_op(vm, number_gt, ra, inst, constants)
        case .Leq:  compare_op(vm, number_leq, ra, inst, constants)
        case .Geq:  compare_op(vm, number_geq, ra, inst, constants)
        case .Not:
            x := get_rk(vm, inst.b, constants)
            value_set_boolean(ra, value_is_falsy(x))
        case .Concat: concat(vm, ra, inst.b, inst.c)
        case .Return:
            // If vararg, keep top as-is
            if n_results := inst.b; n_results != 0 {
                vm.top = mem.ptr_offset(ra, n_results - 1)
            }
            // See: https://www.lua.org/source/5.1/ldo.c.html#luaD_poscall
            // for arg in vm.base[start:ptr_sub(vm.top, vm.base)] {
            //     value_print(arg)
            // }
            return
        }
    }
}

// Rough analog to C macro
@(private="file")
arith_op :: #force_inline proc(vm: ^VM, $op: proc(x, y: f64) -> f64, ra: ^Value, inst: Instruction, constants: []Value) {
    x, y := get_rk_bc(vm, inst, constants)
    if !value_is_number(x) || !value_is_number(y) {
        arith_error(vm, x, y)
        return
    }
    value_set_number(ra, op(x.data.number, y.data.number))
}

arith_error :: proc(vm: ^VM, x, y: Value) {
    tpname := value_type_name(y) if value_is_number(x) else value_type_name(x)
    vm_runtime_error(vm, "perform arithmetic on a %s value", tpname)
}

@(private="file")
compare_op :: #force_inline proc(vm: ^VM, $op: proc(x, y: f64) -> bool, ra: ^Value, inst: Instruction, constants: []Value) {
    x, y := get_rk_bc(vm, inst, constants)
    if !value_is_number(x) || !value_is_number(y) {
        compare_error(vm, x, y)
        return
    }
    value_set_boolean(ra, op(x.data.number, y.data.number))
}

compare_error :: proc(vm: ^VM, x, y: Value) {
    tpname1, tpname2 := value_type_name(x), value_type_name(y)
    if tpname1 == tpname2 {
        vm_runtime_error(vm, "compare 2 %s values", tpname1)
    } else {
        vm_runtime_error(vm, "compare %s with %s", tpname1, tpname2)
    }
}

@(private="file")
concat :: proc(vm: ^VM, ra: ^Value, b, c: u16) {
    builder := &vm.builder
    strings.builder_reset(builder)
    // Add 1 because we want to include Reg[C]
    for arg in vm.base[b:c + 1] {
        if !value_is_string(arg) {
            vm_runtime_error(vm, "concatenate a %s value", value_type_name(arg))
        }
        strings.write_string(builder, ostring_to_string(arg.ostring))
    }
    str := ostring_new(vm, strings.to_string(builder^))
    value_set_string(ra, str)
}

@(private="file")
get_rk_bc :: #force_inline proc(vm: ^VM, inst: Instruction, constants: []Value) -> (rk_b: Value, rk_c: Value) {
    return get_rk(vm, inst.b, constants), get_rk(vm, inst.c, constants)
}

@(private="file")
get_rk :: #force_inline proc(vm: ^VM, b_or_c: u16, constants: []Value) -> Value {
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
