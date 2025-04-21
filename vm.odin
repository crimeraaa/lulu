#+private
package lulu

import "base:intrinsics"
import c "core:c/libc"
import "core:fmt"
import "core:mem"
import "core:strings"

// Minimum stack size guaranteed to be available to all functions
STACK_MIN :: 8

MEMORY_ERROR_STRING :: "Out of memory"

VM :: struct {
    stack:      []Value, // len(stack) == stack_size
    allocator:    mem.Allocator,
    builder:      strings.Builder,
    interned:     Intern,
    globals:      Table,
    objects:     ^Object_Header,
    top, base: [^]Value, // Current stack frame window.
    chunk:       ^Chunk,
    pc:        [^]Instruction, // Next instruction to be executed in the current chunk.
    handlers:    ^Error_Handler,
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
    reset_stack(vm)

    push_fstring(vm, "%s:%i: ", source, line)
    push_fstring(vm, format, ..args)
    concat(vm, 2)
    vm_throw(vm, .Compile_Error)
}

vm_runtime_error :: proc(vm: ^VM, format: string, args: ..any) -> ! {
    chunk := vm.chunk
    index := ptr_index(vm.pc, chunk.code) - 1
    line  := chunk.line[index]
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
    // _G and interned strings are not part of the collectable objects list.
    intern_init(vm, &vm.interned)
    vm.globals.type = .Table
    vm.globals.prev = nil

    vm.builder   = strings.builder_make(allocator)
    vm.allocator = allocator
    vm.chunk     = nil

    // Try to handle initial allocations at startup
    alloc_init :: proc(vm: ^VM, user_ptr: rawptr) {
        vm_grow_stack(vm, STACK_MIN)
        reset_stack(vm)

        // Intern the out of memory string already
        // TODO(2025-04-21): When we have garbage collection, we need to mark
        // this object as uncollectable! Otherwise, when it's popped from the
        // stack, it may be garbage collected.
        push_string(vm, MEMORY_ERROR_STRING)
        pop(vm, 1)

        push_rawvalue(vm, value_make(&vm.globals))
        set_global(vm, "_G")
    }

    ok = (vm_run_protected(vm, alloc_init) == .Ok)
    // Bad stuff happened; free any and all allocations we *did* make
    if !ok {
        vm_destroy(vm)
    }
    return ok
}

vm_check_stack :: proc(vm: ^VM, extra: int) {
    stack_end       := &vm.stack[len(vm.stack) - 1]
    stack_remaining := ptr_sub(cast([^]Value)stack_end, vm.top)
    // Remaining stack slots can't accomodate `extra` values?
    if stack_remaining <= extra {
        vm_grow_stack(vm, extra)
    }
}


/*
**Guarantees**
-   `vm.top` and `vm.base` will point to the correct locations in the new stack.
 */
vm_grow_stack :: proc(vm: ^VM, extra: int) {
    new_size := extra
    // Can we just simply double the stack size?
    if new_size <= len(vm.stack) {
        new_size = len(vm.stack) * 2
    } else {
        new_size += len(vm.stack)
    }
    new_stack, err := make([]Value, new_size, vm.allocator)
    if err != nil {
        vm_memory_error(vm)
    }
    old_stack      := vm.stack
    defer delete(old_stack, vm.allocator)
    copy(new_stack, old_stack)
    vm.stack = new_stack
    vm.top   = &new_stack[ptr_index(vm.top, old_stack)]
    vm.base  = &new_stack[ptr_index(vm.base, old_stack)]
}

vm_destroy :: proc(vm: ^VM) {
    delete(vm.stack, vm.allocator)
    intern_destroy(&vm.interned)
    table_destroy(vm, &vm.globals)

    when DEBUG_TRACE_EXEC {
        // objects_print_all(vm)
    }
    object_free_all(vm)
    strings.builder_destroy(&vm.builder)
    vm.top      = nil
    vm.base     = nil
    vm.objects  = nil
    vm.chunk    = nil
}

vm_interpret :: proc(vm: ^VM, input, name: string) -> Status {
    Data :: struct {
        chunk: ^Chunk,
        input: string,
    }

    data := &Data{chunk = &Chunk{}, input = input}
    chunk_init(data.chunk, name)
    defer chunk_destroy(vm, data.chunk)

    interpret :: proc(vm: ^VM, user_data: rawptr) {
        data  := (cast(^Data)user_data)^
        chunk := data.chunk
        compiler_compile(vm, chunk, data.input)
        vm_check_stack(vm, chunk.stack_used)
        // Set up stack frame
        vm.base  = &vm.stack[0]
        vm.top   = &vm.stack[chunk.stack_used]
        vm.chunk = chunk
        vm.pc    = &chunk.code[0]

        // Zero initialize the current stack frame, especially needed as local
        // variable declarations with empty expressions default to implicit nil
        for &slot in vm.base[:chunk.stack_used] {
            slot = value_make()
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
vm_run_protected :: proc(vm: ^VM, try: Protected_Proc, user_data: rawptr = nil) -> Status {
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
    constants := chunk.constants
    globals   := &vm.globals
    stack     := vm.base[:chunk.stack_used]

    for {
        // We cannot extract 'vm.pc' into a local as it is needed in to `vm_runtime_error`.
        inst := vm.pc[0]
        when DEBUG_TRACE_EXEC {
            index := ptr_index(vm.pc, chunk.code)
            #reverse for &value, reg in stack {
                value_print(value, .Stack)
                if local, ok := chunk_get_local(chunk, reg + 1, index); ok {
                    fmt.printfln(" ; local %s", local)
                } else {
                    fmt.println()
                }
            }
            debug_dump_instruction(chunk, inst, index)
        }
        vm.pc = &vm.pc[1]

        // Most instructions use this!
        ra := &stack[inst.a]
        switch (inst.op) {
        case .Move:
            ra^ = stack[inst.b]
        case .Load_Constant:
            bc := ip_get_Bx(inst)
            ra^ = constants[bc]
        case .Load_Nil:
            // Add 1 because we want to include Reg[B]
            for &slot in stack[inst.a:inst.b + 1] {
                slot = value_make()
            }
        case .Load_Boolean:
            ra^ = value_make(inst.b == 1)
        case .Get_Global:
            key := constants[ip_get_Bx(inst)]
            value, ok := table_get(globals, key)
            if !ok {
                ident := value_to_string(key)
                vm_runtime_error(vm, "read undefined global '%s'", ident)
            }
            ra^ = value
        case .Set_Global:
            key := constants[ip_get_Bx(inst)]
            table_set(vm, globals, key, ra^)
        case .New_Table:
            n_array := fb_to_int(cast(u8)inst.b)
            n_hash  := fb_to_int(cast(u8)inst.c)
            ra^ = value_make(table_new(vm, n_array, n_hash))
        case .Get_Table:
            key := get_rk(vm, inst.c, stack, constants)^
            table: ^Table
            if rb := stack[inst.b]; !value_is_table(rb) {
                index_error(vm, rb)
            } else {
                table = rb.table
            }
            ra^ = table_get(table, key)
        case .Set_Table:
            key   := get_rk(vm, inst.b, stack, constants)^
            value := get_rk(vm, inst.c, stack, constants)^
            if !value_is_table(ra^) {
                index_error(vm, ra^)
            }
            if value_is_nil(key) {
                vm_runtime_error(vm, "set a nil index")
            }

            table_set(vm, ra.table, key, value)
        case .Set_Array:
            // Guaranteed because this only occurs in table constructors
            table  := ra.table
            count  := cast(int)inst.b
            offset := cast(int)(inst.c - 1) * FIELDS_PER_FLUSH
            for i in 1..=count {
                key := value_make(offset + i)
                table_set(vm, table, key, stack[cast(int)inst.a + i])
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
            rb := &stack[inst.b]
            if !value_is_number(rb^) {
                arith_error(vm, rb, rb)
            }
            ra^ = value_make(number_unm(rb.number))
        case .Eq, .Neq:
            rb, rc := get_rkb_rkc(vm, inst, stack, constants)
            equals := value_eq(rb^, rc^)
            ra^ = value_make(equals if inst.op == .Eq else !equals)
        case .Lt:   compare_op(vm, number_lt,  ra, inst, stack, constants)
        case .Gt:   compare_op(vm, number_gt,  ra, inst, stack, constants)
        case .Leq:  compare_op(vm, number_leq, ra, inst, stack, constants)
        case .Geq:  compare_op(vm, number_geq, ra, inst, stack, constants)
        case .Not:
            x := get_rk(vm, inst.b, stack, constants)^
            ra^ = value_make(value_is_falsy(x))
        // Add 1 because we want to include Reg[C]
        case .Concat: vm_concat(vm, ra, stack[inst.b:inst.c + 1])
        case .Len:
            rb := stack[inst.b]
            #partial switch rb.type {
            case .String:
                ra^ = value_make(rb.ostring.len)
            case .Table:
                index := 1
                count := 0
                table := rb.table
                // TODO(2025-04-13): Optimize by separating array from hash!
                for {
                    defer index += 1
                    key      := value_make(index)
                    value, _ := table_get(table, key)
                    if value_is_nil(value) {
                        break
                    } else {
                        count += 1
                    }
                }
                ra^ = value_make(count)
            case:
                vm_runtime_error(vm, "get length of a %s value", value_type_name(rb))
            }

        case .Return:
            // if inst.c != 0 then we have a vararg
            nret := cast(int)inst.b if inst.c == 0 else get_top(vm)
            vm.top = &stack[cast(int)inst.a + nret]
            // See: https://www.lua.org/source/5.1/ldo.c.html#luaD_poscall
            return
        case:
            unreachable()
        }
    }

    ///=== VM EXECUTION HELPERS ============================================ {{{

    get_rkb_rkc :: proc(vm: ^VM, inst: Instruction, stack, constants: []Value) -> (rkb, rkc: ^Value) {
        rkb = get_rk(vm, inst.b, stack, constants)
        rkc = get_rk(vm, inst.c, stack, constants)
        return rkb, rkc
    }

    get_rk :: proc(vm: ^VM, reg: u16, stack, constants: []Value) -> ^Value {
        return &constants[reg_get_k(reg)] if reg_is_k(reg) else &stack[reg]
    }

    // Rough analog to C macro
    arith_op :: proc(vm: ^VM, $op: Number_Arith_Proc, ra: ^Value, inst: Instruction, stack, constants: []Value) {
        left, right := get_rkb_rkc(vm, inst, stack, constants)
        if !value_is_number(left^) || !value_is_number(right^) {
            arith_error(vm, left, right)
        }
        ra^ = value_make(op(left.number, right.number))
    }

    // Rough analog to C macro
    compare_op :: proc(vm: ^VM, $op: Number_Compare_Proc, ra: ^Value, inst: Instruction, stack, constants: []Value) {
        left, right := get_rkb_rkc(vm, inst, stack, constants)
        if !value_is_number(left^) || !value_is_number(right^) {
            compare_error(vm, left, right)
            return
        }
        ra^ = value_make(op(left.number, right.number))
    }

    arith_error :: proc(vm: ^VM, left, right: ^Value) -> ! {
        // left hand side is fine, right hand side is the culprit
        culprit := right if value_is_number(left^) else left
        typname := value_type_name(culprit^)

        // Culprit is in the active stack frame?
        if reg, ok := ptr_index_safe(culprit, vm.base[:get_top(vm)]); ok {
            chunk := vm.chunk

            // Inline implementation `ldebug.c:currentpc()`.
            // `pc` always points to instruction AFTER current, so culprit is
            // at the index of `pc - 1`
            pc := ptr_index(vm.pc, chunk.code) - 1

            // Culprit is a variable or a field?
            if ident, scope, ok := debug_get_variable(chunk, pc, reg); ok {
                vm_runtime_error(vm, "perform arithmetic on %s %q (a %s value)",
                    scope, ident, typname)
            }
        }
        // Default case; we only know its type
        vm_runtime_error(vm, "perform arithmetic on a %s value", typname)
    }

    compare_error :: proc(vm: ^VM, left, right: ^Value) {
        tpname1, tpname2 := value_type_name(left^), value_type_name(right^)
        if tpname1 == tpname2 {
            vm_runtime_error(vm, "compare 2 %s values", tpname1)
        } else {
            vm_runtime_error(vm, "compare %s with %s", tpname1, tpname2)
        }
    }

    index_error :: proc(vm: ^VM, t: Value) -> ! {
        vm_runtime_error(vm, "index a %s value", value_type_name(t))
    }

    ///=== }}} =================================================================
}

vm_concat :: proc(vm: ^VM, ra: ^Value, args: []Value) {
    builder := vm_get_builder(vm)
    for arg in args {
        if !value_is_string(arg) {
            vm_runtime_error(vm, "concatenate a %s value", value_type_name(arg))
        }
        strings.write_string(builder, value_to_string(arg))
    }
    s := strings.to_string(builder^)
    ra^ = value_make(ostring_new(vm, s))
}

@(private="file")
reset_stack :: proc(vm: ^VM) {
    vm.base = &vm.stack[0]
    vm.top  = vm.base
}
