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
    code:   Error,
}

Protected_Proc :: #type proc(vm: ^VM, user_data: rawptr)

/*
Links:
-   https://www.lua.org/source/5.1/ldo.c.html#luaD_throw
 */
vm_throw :: proc(vm: ^VM, code: Error) -> ! {
    if handler := vm.handlers; handler != nil {
        intrinsics.volatile_store(&handler.code, code)
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
    pc     := ptr_index(vm.saved_ip, chunk.code) - 1
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

    // Bad stuff happened; free any and all allocations we *did* make
    if ok = vm_run_protected(vm, alloc_init) == nil; !ok {
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

vm_interpret :: proc(vm: ^VM, input, source: string) -> Error {
    Data :: struct {
        chunk: Chunk,
        input: string,
    }

    data := &Data{input = input}
    chunk_init(&data.chunk, source)
    defer chunk_destroy(vm, &data.chunk)

    interpret :: proc(vm: ^VM, user_data: rawptr) {
        data  := cast(^Data)user_data
        chunk := &data.chunk
        compiler_compile(vm, chunk, data.input)
        vm_check_stack(vm, chunk.stack_used)
        // Set up stack frame
        vm.base  = &vm.stack[0]
        vm.top   = &vm.stack[chunk.stack_used]
        vm.chunk = chunk

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
vm_run_protected :: proc(vm: ^VM, try: Protected_Proc, user_data: rawptr = nil) -> Error {
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
    return intrinsics.volatile_load(&handler.code)
}


/*
**Analogous to**
-   `vm.c:run()` in Crafting Interpreters, Chapter 15.1.1: *Executing
    Instructions*.
-   `lvm.c:luaV_execute(lua_State *L, int nexeccalls)` in Lua 5.1.5.
 */
vm_execute :: proc(vm: ^VM) {
    ///=== VM EXECUTION HELPERS ============================================ {{{

    get_rk :: proc {
        get_rk_one,
        get_rk_two,
    }

    get_rk_two :: proc(read: Instruction, stack, constants: []Value) -> (rkb, rkc: ^Value) {
        rkb = get_rk_one(read.b, stack, constants)
        rkc = get_rk_one(read.c, stack, constants)
        return rkb, rkc
    }

    get_rk_one :: proc(reg: u16, stack, constants: []Value) -> ^Value {
        return &constants[reg_get_k(reg)] if reg_is_k(reg) else &stack[reg]
    }

    // Rough analog to C macro
    arith_op :: proc(vm: ^VM, ip: [^]Instruction, op: Number_Arith_Proc, ra, left, right: ^Value) {
        if !value_is_number(left^) || !value_is_number(right^) {
            arith_error(vm, ip, left, right)
        }
        ra^ = value_make(op(left.number, right.number))
    }

    // Rough analog to C macro
    compare_op :: proc(vm: ^VM, ip: [^]Instruction, op: Number_Compare_Proc, ra, left, right: ^Value) {
        if !value_is_number(left^) || !value_is_number(right^) {
            compare_error(vm, ip, left, right)
        }
        ra^ = value_make(op(left.number, right.number))
    }

    arith_error :: proc(vm: ^VM, ip: [^]Instruction, left, right: ^Value) -> ! {
        protect_begin(vm, ip)
        // true => left hand side is fine, right hand side is the culprit
        culprit := right if value_is_number(left^) else left
        debug_type_error(vm, culprit, "perform arithmetic on")
    }

    compare_error :: proc(vm: ^VM, ip: [^]Instruction, left, right: ^Value) -> ! {
        protect_begin(vm, ip)
        if t1, t2 := value_type_name(left^), value_type_name(right^); t1 == t2 {
            vm_runtime_error(vm, "Attempt to compare 2 %s values", t1)
        } else {
            vm_runtime_error(vm, "Attempt to compare %s with %s", t1, t2)
        }
    }

    /*
    **Analogous to**
    -   `lvm.c:Protect(x)` in Lua 5.1.5.
     */
    protect_begin :: #force_inline proc "contextless" (vm: ^VM, ip: [^]Instruction) {
        vm.saved_ip = ip
    }

    index_error :: #force_inline proc(vm: ^VM, ip: [^]Instruction, culprit: ^Value) -> ! {
        protect_begin(vm, ip)
        debug_type_error(vm, culprit, "index")
    }

    incr_ip :: #force_inline proc "contextless" (ip: ^[^]Instruction, offset := 1) -> Instruction {
        defer ip^ = ptr_offset(ip^, offset)
        // pointer-to-indexable (slice, multipointer) can be indexed directly
        return ip[0]
    }

    ///=== }}} =================================================================

    chunk     := vm.chunk
    constants := chunk.constants
    globals   := &vm.globals
    stack     := vm.base[:chunk.stack_used]
    ip        := raw_data(chunk.code) // Don't deref; use `incr_ip()`

    ip_left_pad    := count_digits(len(chunk.code))
    stack_left_pad := count_digits(len(stack))

    for {
        read := incr_ip(&ip)
        when DEBUG_TRACE_EXEC {
            index := ptr_index(ip, chunk.code) - 1
            for value, reg in stack {
                fmt.printf("\t$% -*i | %d", stack_left_pad, reg, value)
                if local, ok := chunk_get_local(chunk, reg + 1, index); ok {
                    fmt.printfln(" ; %s", local)
                } else {
                    fmt.println()
                }
            }
            debug_dump_instruction(chunk, read, index, ip_left_pad)
        }
        ra := &stack[read.a] // Most instructions use this!
        switch read.op {
        case .Move:
            ra^ = stack[read.b]
        case .Load_Constant:
            bc := ip_get_Bx(read)
            ra^ = constants[bc]
        case .Load_Nil:
            // Add 1 because we want to include Reg[B]
            for &slot in stack[read.a:read.b + 1] {
                slot = value_make()
            }
        case .Load_Boolean:
            ra^ = value_make(read.b == 1)
            if bool(read.c) {
                incr_ip(&ip)
            }
        case .Get_Global:
            key := constants[ip_get_Bx(read)]
            if value, ok := table_get(globals, key); !ok {
                ident := value_to_string(key)
                vm_runtime_error(vm, "Attempt to read undefined global '%s'",
                                 ident)
            } else {
                ra^ = value
            }
        case .Set_Global:
            key := constants[ip_get_Bx(read)]
            table_set(vm, globals, key, ra^)
        case .New_Table:
            n_array := fb_decode(cast(u8)read.b)
            n_hash  := fb_decode(cast(u8)read.c)
            ra^ = value_make(table_new(vm, n_array, n_hash))
        case .Get_Table:
            if rb := &stack[read.b]; !value_is_table(rb^) {
                index_error(vm, ip, rb)
            } else {
                key := get_rk(read.c, stack, constants)^
                ra^ = table_get(rb.table, key)
            }
        case .Set_Table:
            if !value_is_table(ra^) {
                index_error(vm, ip, ra)
            }
            if k, v := get_rk(read, stack, constants); value_is_nil(k^) {
                index_error(vm, ip, k)
            } else {
                table_set(vm, ra.table, k^, v^)
            }
        case .Set_Array:
            // Guaranteed because this only occurs in table constructors
            table  := ra.table
            count  := cast(int)read.b
            offset := cast(int)(read.c - 1) * FIELDS_PER_FLUSH
            for index in 1..=count {
                key := value_make(offset + index)
                table_set(vm, table, key, stack[cast(int)(read.a) + index])
            }
        case .Print:
            for arg in stack[read.a:read.b] {
                fmt.print(arg, ' ', sep = "")
            }
            fmt.println()
        case .Add: arith_op(vm, ip, number_add, ra, get_rk(read, stack, constants))
        case .Sub: arith_op(vm, ip, number_sub, ra, get_rk(read, stack, constants))
        case .Mul: arith_op(vm, ip, number_mul, ra, get_rk(read, stack, constants))
        case .Div: arith_op(vm, ip, number_div, ra, get_rk(read, stack, constants))
        case .Mod: arith_op(vm, ip, number_mod, ra, get_rk(read, stack, constants))
        case .Pow: arith_op(vm, ip, number_pow, ra, get_rk(read, stack, constants))
        case .Unm:
            if rb := &stack[read.b]; !value_is_number(rb^) {
                arith_error(vm, ip, rb, rb)
            } else {
                ra^ = value_make(number_unm(rb.number))
            }
        case .Eq:
            rb, rc := get_rk(read, stack, constants)
            ra^ = value_make(value_eq(rb^, rc^))
        case .Neq:
            rb, rc := get_rk(read, stack, constants)
            ra^ = value_make(!value_eq(rb^, rc^))
        case .Lt:  compare_op(vm, ip, number_lt,  ra, get_rk(read, stack, constants))
        case .Leq: compare_op(vm, ip, number_leq, ra, get_rk(read, stack, constants))
        case .Not:
            x := get_rk(read.b, stack, constants)^
            ra^ = value_make(value_is_falsy(x))
        // Add 1 because we want to include Reg[C]
        case .Concat: vm_concat(vm, ra, stack[read.b:read.c + 1])
        case .Len:
            protect_begin(vm, ip)
            #partial switch rb := &stack[read.b]; rb.type {
            case .String:
                ra^ = value_make(rb.ostring.len)
            case .Table:
                count := 0
                table := rb.table
                // TODO(2025-04-13): Optimize by separating array from hash!
                for index in 1..=table.count {
                    key := value_make(index)
                    if value, _ := table_get(table, key); value_is_nil(value) {
                        break
                    }
                    count += 1
                }
                ra^ = value_make(count)
            case:
                debug_type_error(vm, rb, "get length of")
            }
        case .Test:
            if !value_is_falsy(ra^) != bool(read.c) {
                incr_ip(&ip)
            }
        case .Test_Set:
            if rb := stack[read.b]; !value_is_falsy(rb) == bool(read.c) {
                ra^ = rb
            } else {
                incr_ip(&ip)
            }
        case .Jump:
            offset := ip_get_sBx(read)
            incr_ip(&ip, offset)
        case .Return:
            // if ip.c != 0 then we have a vararg
            nret := cast(int)read.b if read.c == 0 else get_top(vm)
            vm.top = &stack[cast(int)read.a + nret]
            // See: https://www.lua.org/source/5.1/ldo.c.html#luaD_poscall
            return
        case:
            unreachable("Unknown opcode %v", read.op)
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
