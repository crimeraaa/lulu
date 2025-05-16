#+private
package lulu

import "base:intrinsics"
import "core:c/libc"
import "core:fmt"
import "core:mem"
import "core:strings"
import "core:slice"

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
        fmt.eprintln("Unprotected call to Lulu API")
        // Nothing much else can be done in this case.
        libc.exit(libc.EXIT_FAILURE)
    }
}

vm_get_builder :: proc(vm: ^VM) -> (b: ^strings.Builder) {
    b = &vm.builder
    strings.builder_reset(b)
    return b
}

vm_syntax_error :: proc(vm: ^VM, source: string, line: int, format: string, args: ..any) -> ! {
    push_fstring(vm, "%s:%i: ", source, line)
    push_fstring(vm, format, ..args)
    concat(vm, 2)
    vm_throw(vm, .Syntax)
}

vm_runtime_error :: proc(vm: ^VM, format: string, args: ..any) -> ! {
    chunk  := vm.chunk
    source := chunk.source
    pc     := ptr_index(vm.saved_ip, chunk.code) - 1
    line   := chunk.line[pc]

    push_fstring(vm, "%s:%i: ", source, line)
    push_fstring(vm, format, ..args)
    concat(vm, 2)
    vm_throw(vm, .Runtime)
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
    stack_end :=  len(vm.stack_all) - 1
    view_end   := vm_view_absindex(vm, .Top)
    // Remaining stack slots can't accomodate `extra` values?
    if stack_end - view_end <= extra {
        vm_grow_stack(vm, extra)
    }
}

View_Mode :: enum {Base, Top}

/*
**Overview**
-   Get the absolute index of `&vm.view[0]` or `&vm.view[len(vm.view)]` in
    `vm.stack_all`.

**Example**
-   `vm.stack_all` is `[nil, true, false, 3.14, "Hi mom!"]`

```odin
vm.view = vm.stack_all[2:4]
vm_view_absindex(vm, .Base) // view index 0 is stack index 2
vm.view_absindex(vm, .Top)  // view index 2 is stack index 4
```
 */
vm_view_absindex :: proc(vm: ^VM, $mode: View_Mode) -> int {
    return ptr_index(vm_view_ptr(vm, mode), vm.stack_all)
}


/*
**Warning**
-   For `View_Mode.Top`, pointer will be 1 past the last valid element in
    `vm.view`.
-   It is NOT safe to assume you can dereference index 0, or any indexes above.
-   This is because it is possible for `&vm.view[len(vm.view)]` to be the same
    as `&vm.stack_all[len(vm.stack_all)]`.
-   In that case it will point to 1 past the last valid element in the main
    stack.
 */
vm_view_ptr :: proc(vm: ^VM, $mode: View_Mode) -> [^]Value {
    when mode == .Base {
        return raw_data(vm.view)
    } else when mode == .Top {
        return ptr_offset(raw_data(vm.view), len(vm.view))
    } else {
        #panic("Invalid mode")
    }
}


/*
**Guarantees**
-   `vm.top` and `vm.base` will point to the correct locations in the new stack.
 */
vm_grow_stack :: proc(vm: ^VM, extra: int) {
    new_size := extra
    // Can we just simply double the stack size?
    if new_size <= len(vm.stack_all) {
        new_size = len(vm.stack_all) * 2
    } else {
        new_size += len(vm.stack_all)
    }
    // Save the indexes now because the old data will be invalidated
    // This is safe even on the very first call, because *both* `stack_all` and
    // `view` are `nil`. Pointer subtraction, after casting to `uintptr`,
    // results in `0`.
    base := vm_view_absindex(vm, .Base)
    top  := base + get_top(vm)
    slice_resize(vm, &vm.stack_all, new_size)
    vm.view = vm.stack_all[base:top]
}

vm_destroy :: proc(vm: ^VM) {
    delete(vm.stack_all, vm.allocator)
    intern_destroy(&vm.interned)
    table_destroy(vm, &vm.globals)

    when DEBUG_TRACE_EXEC {
        // objects_print_all(vm)
    }
    object_free_all(vm)
    strings.builder_destroy(&vm.builder)
    vm.view     = {}
    vm.objects  = nil
    vm.chunk    = nil
}


/*
**Assumptions**
-   `input` was previously pushed to the VM stack. It probably resides at index
    0 or 1.
-   It is valid for the entire call to `compiler_compile` because the VM stack
    is not modified nor reallocated at any point there.
-   Once we start execution, however, its register will be overriden.
 */
vm_interpret :: proc(vm: ^VM, input, source: string) -> Error {
    Data :: struct {
        chunk:  Chunk,
        input:  string,
    }

    data := &Data{input = input}
    chunk_init(&data.chunk, source)
    defer chunk_destroy(vm, &data.chunk)

    interpret :: proc(vm: ^VM, user_data: rawptr) {
        data  := cast(^Data)user_data
        chunk := &data.chunk
        compiler_compile(vm, chunk, data.input)
        if DEBUG_PRINT_CODE {
            debug_dump_chunk(chunk, len(chunk.code))
        }
        vm_check_stack(vm, chunk.stack_used)
        vm.view  = vm.stack_all[:chunk.stack_used]
        vm.chunk = chunk

        // Zero initialize the current stack frame, especially needed as local
        // variable declarations with empty expressions default to implicit nil
        for &slot in vm.view {
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
    arith_op :: proc(vm: ^VM, ip: [^]Instruction, $op: Number_Arith_Proc, ra, left, right: ^Value) {
        if !value_is_number(left^) || !value_is_number(right^) {
            arith_error(vm, ip, left, right)
        }
        ra^ = value_make(op(left.number, right.number))
    }

    // Rough analog to C macro
    compare_op :: proc(vm: ^VM, ip: ^[^]Instruction, $op: Number_Compare_Proc, left, right: ^Value) {
        if !value_is_number(left^) || !value_is_number(right^) {
            compare_error(vm, ip^, left, right)
        }
        cond := bool(ip[-1].a)
        if op(left.number, right.number) != cond {
            incr_ip(ip)
        }
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

    // Mimic C-style `*ip++`.
    incr_ip :: #force_inline proc "contextless" (ip: ^[^]Instruction, offset := 1) -> Instruction {
        defer ip^ = ptr_offset(ip^, offset)
        // pointer-to-indexable (slice, multipointer) can be indexed directly
        return ip[0]
    }

    ///=== }}} =================================================================

    chunk     := vm.chunk
    constants := chunk.constants
    globals   := &vm.globals
    stack     := vm.view
    ip        := raw_data(chunk.code) // Don't deref; use `incr_ip()`

    when DEBUG_TRACE_EXEC {
        ip_left_pad    := count_digits(len(chunk.code))
        stack_left_pad := count_digits(len(stack))
    }

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

        // Most instructions use this; we also guarantee register 0 and 1
        // are safe to deference no matter what. So for comparisons we can
        // safely move past this load even if it goes unused.
        ra := &stack[read.a]
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
            k := constants[ip_get_Bx(read)]
            if v, ok := table_get(globals, k); !ok {
                ident := value_as_string(k)
                protect_begin(vm, ip)
                vm_runtime_error(vm, "Attempt to read undefined global '%s'",
                                 ident)
            } else {
                ra^ = v
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
            // Guaranteed to work; only occurs in table constructors
            count  := cast(int)read.b
            offset := cast(int)(read.c - 1) * FIELDS_PER_FLUSH
            for index in 1..=count {
                k := value_make(offset + index)
                v := stack[cast(int)read.a + index]
                table_set(vm, ra.table, k, v)
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
            if value_eq(rb^, rc^) != bool(read.a) {
                // Skip the jump which would otherwise load false
                incr_ip(&ip)
            }
        case .Lt:  compare_op(vm, &ip, number_lt,  get_rk(read, stack, constants))
        case .Leq: compare_op(vm, &ip, number_leq, get_rk(read, stack, constants))
        case .Not:
            x := get_rk(read.b, stack, constants)^
            ra^ = value_make(value_is_falsy(x))
        // Add 1 because we want to include Reg[C]
        case .Concat:
            protect_begin(vm, ip)
            vm_concat(vm, ra, stack[read.b:read.c + 1])
        case .Len:
            protect_begin(vm, ip)
            #partial switch rb := &stack[read.b]; rb.type {
            case .String:
                ra^ = value_make(rb.ostring.len)
            case .Table:
                t := rb.table
                // TODO(2025-04-13): Optimize by separating array from hash!
                count := 0
                for index in 1..=t.count {
                    k := value_make(index)
                    if v := table_get(t, k); value_is_nil(v) {
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
            n := cast(int)read.b if read.c == 0 else get_top(vm)
            results := slice.from_ptr(ra, n)
            final   := stack[:n]

            // Overwrite registers 0 up to n; later on, when we have function
            // calls, callers will have their results in the right place.
            for &slot, reg in final {
                slot = results[reg]
            }

            // TODO(2025-05-16): Don't do this when we have function calls;
            // because it will invalidate the caller's stack frame/view!
            vm.view = final

            // See: https://www.lua.org/source/5.1/ldo.c.html#luaD_poscall
            return
        case:
            unreachable("Unknown opcode %v", read.op)
        }
    }
}

vm_concat :: proc(vm: ^VM, ra: ^Value, args: []Value) {
    b := vm_get_builder(vm)
    for &arg in args {
        if !value_is_string(arg) {
            debug_type_error(vm, &arg, "concatenate")
        }
        strings.write_string(b, value_as_string(arg))
    }
    s := strings.to_string(b^)
    ra^ = value_make(ostring_new(vm, s))
}

@(private="file")
reset_stack :: proc(vm: ^VM) {
    vm.view = vm.stack_all[0:0]
}
