#+private
package lulu

import "base:intrinsics"
import "core:c/libc"
import "core:fmt"
import "core:mem"
import "core:strings"

Error_Handler :: struct {
    prev:  ^Error_Handler,
    buffer: libc.jmp_buf,
    status: Status,
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

vm_compile_error :: proc(vm: ^VM, source: string, line: int, format: string, args: ..any) -> ! {
    push_fstring(vm, "%s:%i: ", source, line)
    push_fstring(vm, format, ..args)
    concat(vm, 2)
    vm_throw(vm, .Compile_Error)
}

vm_runtime_error :: proc(vm: ^VM, format: string, args: ..any) -> ! {
    chunk  := vm.chunk
    source := chunk.source
    pc     := ptr_index(vm.pc, chunk.code)
    line   := chunk.line[pc]

    push_fstring(vm, "%s:%i: ", source, line)
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
    // Save the indexes now because the old data will be invalidated
    old_base := ptr_index(vm.base, vm.stack)
    old_top  := ptr_index(vm.top, vm.stack)
    slice_resize(vm, &vm.stack, new_size)
    vm.top   = &vm.stack[old_top]
    vm.base  = &vm.stack[old_base]
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
    // Chain new handler
    handler := Error_Handler{prev = vm.handlers}
    vm.handlers  = &handler

    /*
    NOTE(2025-01-18):
    -   We cannot wrap this in a function because in order for `longjmp` to work,
        the stack frame that called `setjmp` MUST still be valid.
     */
    if libc.setjmp(&handler.buffer) == 0 {
        try(vm, user_data)
    }

    // Restore old handler
    vm.handlers = handler.prev

    // Use volatile because we don't want this to be optimized out.
    return intrinsics.volatile_load(&handler.status)
}


/*
**Analogous to**
-   `vm.c:run()` in Crafting Interpreters, Chapter 15.1.1: *Executing
    Instructions*.
-   `lvm.c:luaV_execute(lua_State *L, int nexeccalls)` in Lua 5.1.5.
 */
vm_execute :: proc(vm: ^VM) {
    ///=== VM EXECUTION HELPERS ============================================ {{{

    get_rkb_rkc :: proc(vm: ^VM, ip: Instruction, stack, constants: []Value) -> (rkb, rkc: ^Value) {
        rkb = get_rk(vm, ip.b, stack, constants)
        rkc = get_rk(vm, ip.c, stack, constants)
        return rkb, rkc
    }

    get_rk :: proc(vm: ^VM, reg: u16, stack, constants: []Value) -> ^Value {
        return &constants[reg_get_k(reg)] if reg_is_k(reg) else &stack[reg]
    }

    // Rough analog to C macro
    arith_op :: proc(vm: ^VM, op: Number_Arith_Proc, ra: ^Value, ip: Instruction, stack, constants: []Value) {
        left, right := get_rkb_rkc(vm, ip, stack, constants)
        if !value_is_number(left^) || !value_is_number(right^) {
            arith_error(vm, left, right)
        }
        ra^ = value_make(op(left.number, right.number))
    }

    // Rough analog to C macro
    compare_op :: proc(vm: ^VM, op: Number_Compare_Proc, ra: ^Value, ip: Instruction, stack, constants: []Value) {
        left, right := get_rkb_rkc(vm, ip, stack, constants)
        if !value_is_number(left^) || !value_is_number(right^) {
            compare_error(vm, left, right)
        }
        ra^ = value_make(op(left.number, right.number))
    }

    arith_error :: proc(vm: ^VM, left, right: ^Value) -> ! {
        // left hand side is fine, right hand side is the culprit
        culprit := right if value_is_number(left^) else left
        debug_type_error(vm, culprit, "perform arithmetic on")
    }

    compare_error :: proc(vm: ^VM, left, right: ^Value) -> ! {
        tpname1, tpname2 := value_type_name(left^), value_type_name(right^)
        if tpname1 == tpname2 {
            vm_runtime_error(vm, "compare 2 %s values", tpname1)
        } else {
            vm_runtime_error(vm, "compare %s with %s", tpname1, tpname2)
        }
    }

    index_error :: #force_inline proc(vm: ^VM, culprit: ^Value) -> ! {
        debug_type_error(vm, culprit, "index")
    }

    incr_top :: #force_inline proc(vm: ^VM, amount := 1) {
        vm.pc = ptr_offset(vm.pc, amount)
    }

    ///=== }}} =================================================================

    chunk     := vm.chunk
    constants := chunk.constants
    globals   := &vm.globals
    stack     := vm.base[:chunk.stack_used]
    for {
        // We cannot extract 'vm.pc' into a local as it is needed in to `vm_runtime_error`.
        ip := vm.pc[0]
        when DEBUG_TRACE_EXEC {
            index := ptr_index(vm.pc, chunk.code)
            #reverse for value, reg in stack {
                if local, ok := chunk_get_local(chunk, reg + 1, index); ok {
                    fmt.printfln("%s ; local %s", value, local)
                } else {
                    fmt.printfln("%s", value)
                }
            }
            debug_dump_instruction(chunk, ip, index)
        }
        vm.pc = &vm.pc[1]

        // Most instructions use this!
        ra := &stack[ip.a]
        switch ip.op {
        case .Move:
            ra^ = stack[ip.b]
        case .Load_Constant:
            bc := ip_get_Bx(ip)
            ra^ = constants[bc]
        case .Load_Nil:
            // Add 1 because we want to include Reg[B]
            for &slot in stack[ip.a:ip.b + 1] {
                slot = value_make()
            }
        case .Load_Boolean:
            ra^ = value_make(ip.b == 1)
            if ip.c == 1 {
                incr_top(vm)
            }
        case .Get_Global:
            key := constants[ip_get_Bx(ip)]
            value, ok := table_get(globals, key)
            if !ok {
                ident := value_to_string(key)
                vm_runtime_error(vm, "Attempt to read undefined global '%s'", ident)
            }
            ra^ = value
        case .Set_Global:
            key := constants[ip_get_Bx(ip)]
            table_set(vm, globals, key, ra^)
        case .New_Table:
            n_array := fb_decode(cast(u8)ip.b)
            n_hash  := fb_decode(cast(u8)ip.c)
            ra^ = value_make(table_new(vm, n_array, n_hash))
        case .Get_Table:
            key := get_rk(vm, ip.c, stack, constants)^
            table: ^Table
            if rb := &stack[ip.b]; !value_is_table(rb^) {
                index_error(vm, rb)
            } else {
                table = rb.table
            }
            ra^ = table_get(table, key)
        case .Set_Table:
            key   := get_rk(vm, ip.b, stack, constants)
            value := get_rk(vm, ip.c, stack, constants)^
            if !value_is_table(ra^) {
                index_error(vm, ra)
            }
            if value_is_nil(key^) {
                debug_type_error(vm, key, "set a nil index")
            }
            table_set(vm, ra.table, key^, value)
        case .Set_Array:
            // Guaranteed because this only occurs in table constructors
            table  := ra.table
            count  := cast(int)ip.b
            offset := cast(int)(ip.c - 1) * FIELDS_PER_FLUSH
            for i in 1..=count {
                key := value_make(offset + i)
                table_set(vm, table, key, stack[cast(int)ip.a + i])
            }
        case .Print:
            for arg in stack[ip.a:ip.b] {
                fmt.print(arg, '\t', sep = "")
            }
            fmt.println()
        case .Add: arith_op(vm, number_add, ra, ip, stack, constants)
        case .Sub: arith_op(vm, number_sub, ra, ip, stack, constants)
        case .Mul: arith_op(vm, number_mul, ra, ip, stack, constants)
        case .Div: arith_op(vm, number_div, ra, ip, stack, constants)
        case .Mod: arith_op(vm, number_mod, ra, ip, stack, constants)
        case .Pow: arith_op(vm, number_pow, ra, ip, stack, constants)
        case .Unm:
            rb := &stack[ip.b]
            if !value_is_number(rb^) {
                arith_error(vm, rb, rb)
            }
            ra^ = value_make(number_unm(rb.number))
        case .Eq:
            rb, rc := get_rkb_rkc(vm, ip, stack, constants)
            ra^ = value_make(value_eq(rb^, rc^))
        case .Neq:
            rb, rc := get_rkb_rkc(vm, ip, stack, constants)
            ra^ = value_make(!value_eq(rb^, rc^))
        case .Lt:   compare_op(vm, number_lt,  ra, ip, stack, constants)
        case .Leq:  compare_op(vm, number_leq, ra, ip, stack, constants)
        case .Not:
            x := get_rk(vm, ip.b, stack, constants)^
            ra^ = value_make(value_is_falsy(x))
        // Add 1 because we want to include Reg[C]
        case .Concat: vm_concat(vm, ra, stack[ip.b:ip.c + 1])
        case .Len:
            rb := &stack[ip.b]
            #partial switch rb.type {
            case .String:
                ra^ = value_make(rb.ostring.len)
            case .Table:
                count := 0
                table := rb.table
                // TODO(2025-04-13): Optimize by separating array from hash!
                for index in 1..<table.count {
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
                debug_type_error(vm, rb, "get length of")
            }
        case .Test:
            if !value_is_falsy(ra^) != bool(ip.c) {
                incr_top(vm)
            }
        case .Test_Set:
            rb := stack[ip.b]
            if !value_is_falsy(rb) == bool(ip.c) {
                ra^ = rb
            } else {
                incr_top(vm)
            }
        case .Jump:
            offset := ip_get_sBx(ip)
            incr_top(vm, offset)
        case .Return:
            // if ip.c != 0 then we have a vararg
            nret := cast(int)ip.b if ip.c == 0 else get_top(vm)
            vm.top = &stack[cast(int)ip.a + nret]
            // See: https://www.lua.org/source/5.1/ldo.c.html#luaD_poscall
            return
        case:
            unreachable("Unknown opcode %v", ip.op)
        }
    }
}

vm_concat :: proc(vm: ^VM, ra: ^Value, args: []Value) {
    builder := vm_get_builder(vm)
    for &arg in args {
        if !value_is_string(arg) {
            debug_type_error(vm, &arg, "concatenate")
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
