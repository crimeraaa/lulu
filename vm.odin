#+private
package lulu

import "base:intrinsics"
import "core:c/libc"
import "core:fmt"
import "core:mem"
import "core:strings"

STACK_MAX :: 256

VM :: struct {
    stack:     [STACK_MAX]Value,
    allocator: mem.Allocator,
    builder:   strings.Builder,
    interned:  Intern,
    globals:   Table,
    objects:  ^Object_Header,
    top, base: int, // Absolute indexes of current stack frame window.
    chunk:    ^Chunk,
    pc:        int, // Index of next instruction to be executed in the current chunk.
    handlers: ^Error_Handler,
}

Error_Handler :: struct {
    prev:  ^Error_Handler,
    buffer: libc.jmp_buf,
    status: Status,
}

Status :: enum {
    Ok,
    Compile_Error,
    Runtime_Error,
}

Try_Proc :: #type proc(vm: ^VM, user_data: rawptr)

/*
Links:
-   https://www.lua.org/source/5.1/ldo.c.html#luaD_throw
 */
vm_throw :: proc(vm: ^VM, status: Status, source: string, line: int, format: string, args: ..any) -> ! {
    push_fstring(vm, "%s:%i ", source, line)
    push_fstring(vm, format, ..args)
    concat(vm, 2)

    handler := vm.handlers
    if handler != nil {
        intrinsics.volatile_store(&handler.status, status)
        libc.longjmp(&handler.buffer, 1)
    } else {
        // Nothing much else can be done in this case.
        libc.exit(libc.EXIT_FAILURE)
    }
}

vm_get_builder :: proc(vm: ^VM) -> (builder: ^strings.Builder) {
    builder = &vm.builder
    strings.builder_reset(builder)
    return builder
}

vm_runtime_error :: proc(vm: ^VM, $format: string, args: ..any) -> ! {
    chunk := vm.chunk
    line  := chunk.line[vm.pc - 1]
    reset_stack(vm)
    vm_throw(vm, .Runtime_Error, chunk.source, line, "Attempt to " + format, ..args)
}

vm_init :: proc(vm: ^VM, allocator: mem.Allocator) {
    reset_stack(vm)

    // _G and interned strings are not part of the collectable objects list.
    intern_init(vm, &vm.interned)
    vm.globals.type = .Table
    vm.globals.prev = nil

    vm.builder   = strings.builder_make(allocator)
    vm.allocator = allocator
    vm.chunk     = nil
}

vm_destroy :: proc(vm: ^VM) {
    reset_stack(vm)
    intern_destroy(&vm.interned)
    table_destroy(vm, &vm.globals)
    object_free_all(vm)
    strings.builder_destroy(&vm.builder)
    vm.objects  = nil
    vm.chunk    = nil
}

vm_interpret :: proc(vm: ^VM, input, name: string) -> (status: Status) {
    Data :: struct {
        chunk: ^Chunk,
        input: string,
    }

    data := &Data{chunk = &Chunk{}, input = input}
    chunk_init(vm, data.chunk, name)
    defer chunk_destroy(vm, data.chunk)

    interpret :: proc(vm: ^VM, user_data: rawptr) {
        data := cast(^Data)user_data
        compiler_compile(vm, data.chunk, data.input)
        vm.chunk = data.chunk
        vm.pc    = 0
        vm_execute(vm)
    }

    return vm_try(vm, interpret, data)
}

/*
Brief:
-   Wraps the call to `try(vm, user_data)` with an error handler. This allows us
    to catch errors without killing the program.

Analogous to:
-   `ldo.c:luaD_rawrunprotected(lua_State *L, Pfunc f, void *ud)` in Lua 5.1.

Links:
-   https://www.lua.org/source/5.1/ldo.c.html#luaD_rawrunprotected
 */
vm_try :: proc(vm: ^VM, try: Try_Proc, user_data: rawptr) -> (status: Status) {
    handler: Error_Handler
    // Chain new handler
    handler.prev = vm.handlers
    vm.handlers  = &handler

    // Restore old handler
    defer vm.handlers = handler.prev

    /*
    NOTE(2025-01-18):
    -   We cannot wrap this in a function because in order for `longjmp` to work,
        the stack frame that called `setjmp` MUST still be valid.
     */
    if libc.setjmp(&handler.buffer) == 0 {
        try(vm, user_data)
    }

    // Use volatile because we don't want this to be optimized out.
    return intrinsics.volatile_load(&handler.status)
}

// Analogous to 'vm.c:run()' in the book.
vm_execute :: proc(vm: ^VM) {
    chunk     := vm.chunk
    code      := chunk.code[:]
    constants := chunk.constants[:]
    globals   := &vm.globals
    stack     := vm.stack[:]
    ra: ^Value

    for {
        // We cannot extract 'vm.pc' into a local as it is needed in to `vm_runtime_error`.
        inst := code[vm.pc]
        when DEBUG_TRACE_EXEC {
            fmt.printf("      ")
            for value in stack[vm.base:vm.top] {
                value_print(value, .Stack)
            }
            fmt.println()
            debug_dump_instruction(chunk^, inst, vm.pc)
        }
        vm.pc += 1

        // Most instructions use this!
        ra = &stack[inst.a]
        switch (inst.op) {
        case .Load_Constant:
            bc := inst_get_Bx(inst)
            ra^ = constants[bc]
        case .Load_Nil:
            // Add 1 because we want to include Reg[B]
            for &value in stack[inst.a:inst.b + 1] {
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
            table_set(vm, globals, key, ra^)
        case .Print:
            for arg in stack[inst.a:inst.b] {
                value_print(arg, .Print)
            }
            fmt.println()
        case .Add: arith_op(vm, number_add, ra, inst, stack, constants)
        case .Sub: arith_op(vm, number_sub, ra, inst, stack, constants)
        case .Mul: arith_op(vm, number_mul, ra, inst, stack, constants)
        case .Div: arith_op(vm, number_div, ra, inst, stack, constants)
        case .Mod: arith_op(vm, number_mod, ra, inst, stack, constants)
        case .Pow: arith_op(vm, number_pow, ra, inst, stack, constants)
        case .Unm:
            rb := stack[inst.b]
            if !value_is_number(rb) {
                arith_error(vm, rb, rb)
            }
            value_set_number(ra, number_unm(rb.data.number))
        case .Eq, .Neq:
            rb, rc := get_rk_bc(vm, inst, stack, constants)
            b := value_eq(rb, rc)
            value_set_boolean(ra, b if inst.op == .Eq else !b)
        case .Lt:   compare_op(vm, number_lt,  ra, inst, stack, constants)
        case .Gt:   compare_op(vm, number_gt,  ra, inst, stack, constants)
        case .Leq:  compare_op(vm, number_leq, ra, inst, stack, constants)
        case .Geq:  compare_op(vm, number_geq, ra, inst, stack, constants)
        case .Not:
            x := get_rk(vm, inst.b, stack, constants)
            value_set_boolean(ra, value_is_falsy(x))
        // Add 1 because we want to include Reg[C]
        case .Concat: vm_concat(vm, ra, stack[inst.b:inst.c + 1])
        case .Return:
            // If vararg, keep top as-is
            if n_results := inst.b; n_results != 0 {
                new_top := inst.a + (n_results - 1)
                vm.top   = cast(int)new_top
            }
            // See: https://www.lua.org/source/5.1/ldo.c.html#luaD_poscall
            return
        }
    }
}

// Rough analog to C macro
@(private="file")
arith_op :: proc(vm: ^VM, $op: Number_Arith_Proc, ra: ^Value, inst: Instruction, stack, constants: []Value) {
    x, y := get_rk_bc(vm, inst, stack, constants)
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
compare_op :: proc(vm: ^VM, $op: Number_Compare_Proc, ra: ^Value, inst: Instruction, stack, constants: []Value) {
    x, y := get_rk_bc(vm, inst, stack, constants)
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

vm_concat :: proc(vm: ^VM, ra: ^Value, args: []Value) {
    builder := vm_get_builder(vm)
    for arg in args {
        if !value_is_string(arg) {
            vm_runtime_error(vm, "concatenate a %s value", value_type_name(arg))
        }
        strings.write_string(builder, ostring_to_string(arg.ostring))
    }
    s := strings.to_string(builder^)
    value_set_string(ra, ostring_new(vm, s))
}

@(private="file")
get_rk_bc :: proc(vm: ^VM, inst: Instruction, stack, constants: []Value) -> (rk_b: Value, rk_c: Value) {
    return get_rk(vm, inst.b, stack, constants), get_rk(vm, inst.c, stack, constants)
}

@(private="file")
get_rk :: proc(vm: ^VM, b_or_c: u16, stack, constants: []Value) -> Value {
    return constants[rk_get_k(b_or_c)] if rk_is_k(b_or_c) else stack[b_or_c]
}

@(private="file")
reset_stack :: proc(vm: ^VM) {
    vm.base = 0
    vm.top  = vm.base
}

// `mem.ptr_sub` seems to be off by +1
ptr_sub :: proc(a, b: ^$T) -> int {
    return cast(int)(uintptr(a) - uintptr(b)) / size_of(T)
}
