#+private
package lulu

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
}

Status :: enum u8 {
    Ok,
    Compile_Error,
    Runtime_Error,
}

Runtime_Error_Type :: enum {
    None,
    Arith,
    Compare,
    Concat,
    Undefined_Global,
}

// "Attempt to" ...
runtime_error_strings := [Runtime_Error_Type]string {
    .None               = "(empty)",
    .Arith              = "perform arithmetic on",
    .Compare            = "compare",
    .Concat             = "concatenate",
    .Undefined_Global   = "read undefined global"
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

    if !compiler_compile(vm, chunk, input) {
        return .Compile_Error
    }
    vm.chunk = chunk
    vm.ip    = raw_data(chunk.code)
    if run_err := vm_execute(vm); run_err != nil {
        line := chunk.line[ptr_sub(vm.ip, raw_data(chunk.code))]
        fmt.eprintf("%s:%i: Attempt to %s ", chunk.source, line, runtime_error_strings[run_err])
        switch run_err {
        case .None:
            unreachable()
        case .Arith, .Compare, .Concat:
            rk_b, rk_c := get_rk_bc(vm, vm.ip[-1], vm.chunk.constants[:])
            fmt.eprintfln("a %s value and %s",
                           value_type_name(rk_b), value_type_name(rk_c))
        case .Undefined_Global:
            key := chunk.constants[inst_get_Bx(vm.ip[-1])]
            assert(value_is_string(key))
            fmt.eprintfln("%q", ostring_to_string(key.ostring))
        }
        reset_stack(vm)
        return .Runtime_Error
    }
    return .Ok
}

// Analogous to 'vm.c:run()' in the book.
vm_execute :: proc(vm: ^VM) -> (error: Runtime_Error_Type) {
    chunk     := vm.chunk
    code      := chunk.code[:]
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
                return .Undefined_Global
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
        case .Add: arith_op(vm, number_add, ra, inst, constants) or_return
        case .Sub: arith_op(vm, number_sub, ra, inst, constants) or_return
        case .Mul: arith_op(vm, number_mul, ra, inst, constants) or_return
        case .Div: arith_op(vm, number_div, ra, inst, constants) or_return
        case .Mod: arith_op(vm, number_mod, ra, inst, constants) or_return
        case .Pow: arith_op(vm, number_pow, ra, inst, constants) or_return
        case .Unm:
            rb := vm.base[inst.b]
            if !value_is_number(rb) {
                return .Arith
            }
            value_set_number(ra, number_unm(rb.data.number))
        case .Eq, .Neq:
            rb, rc := get_rk_bc(vm, inst, constants)
            b := value_eq(rb, rc)
            value_set_boolean(ra, b if inst.op == .Eq else !b)
        case .Lt:   compare_op(vm, number_lt, ra, inst, constants) or_return
        case .Gt:   compare_op(vm, number_gt, ra, inst, constants) or_return
        case .Leq:  compare_op(vm, number_leq, ra, inst, constants) or_return
        case .Geq:  compare_op(vm, number_geq, ra, inst, constants) or_return
        case .Not:
            x := get_rk(vm, inst.b, constants)
            value_set_boolean(ra, value_is_falsy(x))
        case .Concat: concat(vm, ra, inst.b, inst.c) or_return
        case .Return:
            start := inst.a
            // If vararg, keep top as-is
            if n_results := inst.b; n_results != 0 {
                vm.top = mem.ptr_offset(ra, n_results - 1)
            }
            // See: https://www.lua.org/source/5.1/ldo.c.html#luaD_poscall
            // for arg in vm.base[start:ptr_sub(vm.top, vm.base)] {
            //     value_print(arg)
            // }
            return nil
        }
    }
}

// Rough analog to C macro
@(private="file", require_results)
arith_op :: #force_inline proc(vm: ^VM, $op: proc(x, y: f64) -> f64, ra: ^Value, inst: Instruction, constants: []Value) -> Runtime_Error_Type {
    x, y := get_rk_bc(vm, inst, constants)
    if !value_is_number(x) || !value_is_number(y) {
        return .Arith
    }
    value_set_number(ra, op(x.data.number, y.data.number))
    return nil
}

@(private="file", require_results)
compare_op :: #force_inline proc(vm: ^VM, $op: proc(x, y: f64) -> bool, ra: ^Value, inst: Instruction, constants: []Value) -> Runtime_Error_Type {
    x, y := get_rk_bc(vm, inst, constants)
    if !value_is_number(x) || !value_is_number(y) {
        return .Compare
    }
    value_set_boolean(ra, op(x.data.number, y.data.number))
    return nil
}

@(private="file", require_results)
concat :: proc(vm: ^VM, ra: ^Value, b, c: u16) -> Runtime_Error_Type {
    builder := &vm.builder
    strings.builder_reset(builder)
    // Add 1 because we want to include Reg[C]
    for arg in vm.base[b:c + 1] {
        if !value_is_string(arg) {
            return .Concat
        }
        strings.write_string(builder, ostring_to_string(arg.ostring))
    }
    str := ostring_new(vm, strings.to_string(builder^))
    value_set_string(ra, str)
    return nil
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
