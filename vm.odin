#+private
package lulu

import "base:intrinsics"
import c "core:c/libc"
import "core:fmt"
import "core:mem"
import "core:strings"

STACK_MAX :: 256
MEMORY_ERROR_STRING :: "Out of memory"

VM :: struct {
    stack:     [STACK_MAX]Value,
    allocator: mem.Allocator,
    builder:   strings.Builder,
    interned:  Intern,
    globals:   Table,
    objects:  ^Object_Header,
    top, base: [^]Value, // Current stack frame window.
    chunk:    ^Chunk,
    pc:        [^]Instruction, // Next instruction to be executed in the current chunk.
    handlers: ^Error_Handler,
}

Error_Handler :: struct {
    prev:  ^Error_Handler,
    buffer: c.jmp_buf,
    status: Status,
}

Status :: enum {
    Ok,
    Compile_Error,
    Runtime_Error,
    Out_Of_Memory,
}

Protected_Proc :: #type proc(vm: ^VM, user_data: rawptr)

/*
Links:
-   https://www.lua.org/source/5.1/ldo.c.html#luaD_throw
 */
vm_throw :: proc(vm: ^VM, status: Status) -> ! {
    handler := vm.handlers
    if handler != nil {
        intrinsics.volatile_store(&handler.status, status)
        c.longjmp(&handler.buffer, 1)
    } else {
        // Nothing much else can be done in this case.
        c.exit(c.EXIT_FAILURE)
    }
}

vm_get_builder :: proc(vm: ^VM) -> (builder: ^strings.Builder) {
    builder = &vm.builder
    strings.builder_reset(builder)
    return builder
}

vm_compile_error :: proc(vm: ^VM, source: string, line: int, format: string, args: ..any) -> ! {
    chunk := vm.chunk
    reset_stack(vm)

    push_fstring(vm, "%s:%i: ", source, line)
    push_fstring(vm, format, ..args)
    concat(vm, 2)
    vm_throw(vm, .Compile_Error)
}

vm_runtime_error :: proc(vm: ^VM, format: string, args: ..any) -> ! {
    chunk := vm.chunk
    line  := chunk.line[ptr_sub(vm.pc, &chunk.code[0]) - 1]
    reset_stack(vm)
    push_fstring(vm, "%s:%i: Attempt to ", chunk.source, line)
    push_fstring(vm, format, ..args)
    concat(vm, 2)
    vm_throw(vm, .Runtime_Error)
}

vm_memory_error :: proc(vm: ^VM) -> ! {
    push_string(vm, MEMORY_ERROR_STRING)
    vm_throw(vm, .Out_Of_Memory)
}

@(require_results)
vm_init :: proc(vm: ^VM, allocator: mem.Allocator) -> (ok: bool) {
    reset_stack(vm)

    // _G and interned strings are not part of the collectable objects list.
    intern_init(vm, &vm.interned)
    vm.globals.type = .Table
    vm.globals.prev = nil

    vm.builder   = strings.builder_make(allocator)
    vm.allocator = allocator
    vm.chunk     = nil

    // Try to handle initial allocations at startup
    alloc_init :: proc(vm: ^VM, user_ptr: rawptr) {
        // Intern the out of memory string already
        push_string(vm, MEMORY_ERROR_STRING)
        pop(vm, 1)

        push_rawvalue(vm, value_make_table(&vm.globals))
        set_global(vm, "_G")
    }

    return vm_run_protected(vm, alloc_init) == .Ok
}

vm_destroy :: proc(vm: ^VM) {
    reset_stack(vm)
    intern_destroy(&vm.interned)
    table_destroy(vm, &vm.globals)

    objects_print_all(vm)
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
        data  := cast(^Data)user_data
        chunk := data.chunk
        compiler_compile(vm, chunk, data.input)
        vm.top   = &vm.stack[chunk.stack_used]
        vm.chunk = chunk
        vm.pc    = &chunk.code[0]

        // Zero initialize the current stack frame, especially needed as local
        // variable declarations with empty expressions default to implicit nil
        for &slot in vm.stack[:chunk.stack_used] {
            value_set_nil(&slot)
        }
        vm_execute(vm)
    }

    return vm_run_protected(vm, interpret, data)
}


/*
Overview:
-   Wraps the call to `try(vm, user_data)` with an error handler. This allows us
    to catch errors without killing the program.

Analogous to:
-   `ldo.c:luaD_rawrunprotected(lua_State *L, Pfunc f, void *ud)` in Lua 5.1.5.

Links:
-   https://www.lua.org/source/5.1/ldo.c.html#luaD_rawrunprotected
 */
vm_run_protected :: proc(vm: ^VM, try: Protected_Proc, user_data: rawptr = nil) -> (status: Status) {
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
    if c.setjmp(&handler.buffer) == 0 {
        try(vm, user_data)
    }

    // Use volatile because we don't want this to be optimized out.
    return intrinsics.volatile_load(&handler.status)
}


/*
Analogous to:
-   'vm.c:run()' in the book.
 */
vm_execute :: proc(vm: ^VM) {
    chunk     := vm.chunk
    code      := chunk.code[:]
    constants := chunk.constants[:]
    globals   := &vm.globals
    stack     := vm.stack[:]

    for {
        // We cannot extract 'vm.pc' into a local as it is needed in to `vm_runtime_error`.
        inst := vm.pc[0]
        when DEBUG_TRACE_EXEC {
            fmt.printf("      ")
            for value in vm.base[:ptr_sub(vm.top, vm.base)] {
                value_print(value, .Stack)
            }
            fmt.println()
            debug_dump_instruction(chunk^, inst, ptr_sub(vm.pc, &code[0]))
        }
        vm.pc = &vm.pc[1]

        // Most instructions use this!
        ra := &stack[inst.a]
        switch (inst.op) {
        case .Move:
            ra^ = stack[inst.b]
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
        case .New_Table:
            count_array := fb_to_int(cast(u8)inst.b)
            count_hash  := fb_to_int(cast(u8)inst.c)
            ra^ = value_make_table(table_new(vm, count_array, count_hash))
        case .Get_Table:
            key := get_rk(vm, inst.c, stack, constants)
            table: ^Table
            if t := vm.base[inst.b]; !value_is_table(t) {
                index_error(vm, t)
            } else {
                table = t.table
            }
            ra^ = table_get(table, key)
        case .Set_Table:
            key   := get_rk(vm, inst.b, stack, constants)
            value := get_rk(vm, inst.c, stack, constants)
            if !value_is_table(ra^) {
                index_error(vm, ra^)
            }
            if value_is_nil(key) {
                vm_runtime_error(vm, "set a nil index")
            }

            table_set(vm, ra.table, key, value)
        case .Set_Array:
            // Guaranteed because this only occurs in table constructors
            table := ra.table
            count := cast(int)inst_get_Bx(inst)
            for i in 0..<count {
                key   := value_make_number(cast(f64)i + 1)
                value := vm.top[-count + i]
                table_set(vm, table, key, value)
            }
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
        case .Len:
            rb := stack[inst.b]
            #partial switch rb.type {
            case .String:
                ra^ = value_make_number(cast(f64)rb.ostring.len)
            case .Table:
                index := 1
                count := 0
                table := rb.table
                // TODO(2025-04-13): Optimize by separating array from hash!
                for {
                    defer index += 1
                    key := value_make_number(cast(f64)index)
                    value, _ := table_get(table, key)
                    if value_is_nil(value) {
                        break
                    } else {
                        count += 1
                    }
                }
                ra^ = value_make_number(cast(f64)count)
            case:
                vm_runtime_error(vm, "get length of a %s value", value_type_name(rb))
            }

        case .Return:
            // If vararg, keep top as-is
            if count_results := inst.b; count_results != 0 {
                vm.top = &stack[inst.a + (count_results - 1)]
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
    }
    value_set_number(ra, op(x.data.number, y.data.number))
}

@(private="file")
arith_error :: proc(vm: ^VM, x, y: Value) -> ! {
    tpname := value_type_name(y) if value_is_number(x) else value_type_name(x)
    vm_runtime_error(vm, "perform arithmetic on a %s value", tpname)
}

@(private="file")
index_error :: proc(vm: ^VM, t: Value) -> ! {
    vm_runtime_error(vm, "index a %s value", value_type_name(t))
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

@(private="file")
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
    vm.base = &vm.stack[0]
    vm.top  = vm.base
}

// `mem.ptr_sub` seems to be off by +1
ptr_sub :: proc(a, b: ^$T) -> int {
    return cast(int)(uintptr(a) - uintptr(b)) / size_of(T)
}
