#+private
package lulu

import "base:intrinsics"
import "core:container/small_array"
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

Frame :: struct {
    function:   ^Function,
    ip:       [^]Instruction,
    window:    []Value,
}

MAX_FRAMES :: 64

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
    chunk  := &vm.current.function.chunk
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
    vm.globals   = {type = .Table, next = nil}
    vm.builder   = strings.builder_make(allocator)
    vm.allocator = allocator

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
    stack_end := len(vm.stack_all) - 1
    view_end  := vm_view_absindex(vm, .Top)
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
vm_view_absindex(vm, .Top)  // view index 2 is stack index 4
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

**TODO(2025-06-01)**
-   Correct *all* active `Frame::window` in the `vm.frames` array.
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
    intern_destroy(vm, &vm.interned)
    table_destroy(vm, &vm.globals)

    when DEBUG_TRACE_EXEC {
        // objects_print_all(vm)
    }
    object_free_all(vm)
    strings.builder_destroy(&vm.builder)
    vm.view     = {}
    vm.objects  = nil
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
        source: string,
        input:  string,
    }

    data := &Data{source = source, input = input}
    interpret :: proc(vm: ^VM, user_data: rawptr) {
        data  := cast(^Data)user_data
        fmain := parser_program(vm, data.source, data.input)
        if DEBUG_PRINT_CODE {
            debug_dump_chunk(&fmain.chunk, len(fmain.chunk.code))
        }
        // Need to accomodate the main function itself as well
        n := fmain.chunk.stack_used + 1
        vm_check_stack(vm, n)

        // Users cannot (and should not!) poke at the main function.
        vm.stack_all[0] = value_make(fmain)

        w  := vm.stack_all[1:n]
        cf := small_array.get_ptr(&vm.frames, 0)
        vm.frames.len   += 1
        vm.current = cf
        cf.function      = fmain
        cf.window        = w
        cf.ip            = raw_data(fmain.chunk.code)

        // Zero initialize the current stack frame, especially needed as local
        // variable declarations with empty expressions default to implicit nil
        for &slot in w {
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
    handler    := Error_Handler{prev = vm.handlers}
    vm.handlers = &handler

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
-   `lvm.c:vm_Protect(x)` in Lua 5.1.5.
 */
vm_protect :: #force_inline proc "contextless" (vm: ^VM) {
    vm.saved_ip = vm.current.ip
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

    get_rk_two :: proc(read: Instruction, frame: ^Frame) -> (rkb, rkc: ^Value) {
        rkb = get_rk_one(read.b, frame)
        rkc = get_rk_one(read.c, frame)
        return rkb, rkc
    }

    get_rk_one :: proc(reg: u16, frame: ^Frame) -> ^Value {
        constants := frame.function.chunk.constants
        window    := frame.window
        return &constants[reg_get_k(reg)] if reg_is_k(reg) else &window[reg]
    }

    // Rough analog to C macro
    arith_op :: proc(vm: ^VM, $op: Number_Arith_Proc, ra, left, right: ^Value) {
        if !value_is_number(left^) || !value_is_number(right^) {
            arith_error(vm, left, right)
        }
        ra^ = value_make(op(left.number, right.number))
    }

    // Rough analog to C macro
    compare_op :: proc(vm: ^VM, $op: Number_Compare_Proc, cond: bool, left, right: ^Value) {
        if !value_is_number(left^) || !value_is_number(right^) {
            compare_error(vm, left, right)
        }
        if op(left.number, right.number) != cond {
            incr_ip(&vm.current.ip)
        }
    }

    arith_error :: proc(vm: ^VM, left, right: ^Value) -> ! {
        vm_protect(vm)
        // true => left hand side is fine, right hand side is the culprit
        culprit := right if value_is_number(left^) else left
        debug_type_error(vm, culprit, "perform arithmetic on")
    }

    compare_error :: proc(vm: ^VM, left, right: ^Value) -> ! {
        vm_protect(vm)
        if t1, t2 := value_type_name(left^), value_type_name(right^); t1 == t2 {
            vm_runtime_error(vm, "Attempt to compare 2 %s values", t1)
        } else {
            vm_runtime_error(vm, "Attempt to compare %s with %s", t1, t2)
        }
    }

    index_error :: #force_inline proc(vm: ^VM, culprit: ^Value) -> ! {
        vm_protect(vm)
        debug_type_error(vm, culprit, "index")
    }

    // Mimic C-style `*ip++`.
    incr_ip :: #force_inline proc "contextless" (ip: ^[^]Instruction, offset := 1) -> Instruction {
        defer ip^ = ptr_offset(ip^, offset)
        // pointer-to-indexable (slice, multipointer) can be indexed directly
        return ip[0]
    }

    ///=== }}} =================================================================

    frame   := vm.current
    chunk   := &frame.function.chunk
    globals := &vm.globals

    when DEBUG_TRACE_EXEC {
        ip_left_pad    := count_digits(len(chunk.code))
        stack_left_pad := count_digits(len(frame.window))
    }

    for {
        read := incr_ip(&frame.ip)
        when DEBUG_TRACE_EXEC {
            index := ptr_index(frame.ip, chunk.code) - 1
            for value, reg in frame.window {
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
        ra := &frame.window[read.a]
        switch read.op {
        case .Move:
            ra^ = frame.window[read.b]
        case .Load_Constant:
            bc := ip_get_Bx(read)
            ra^ = chunk.constants[bc]
        case .Load_Nil:
            // Add 1 because we want to include Reg[B]
            for &slot in frame.window[read.a:read.b + 1] {
                slot = value_make()
            }
        case .Load_Boolean:
            ra^ = value_make(read.b == 1)
            if bool(read.c) {
                incr_ip(&frame.ip)
            }
        case .Get_Global:
            k := chunk.constants[ip_get_Bx(read)]
            if v, ok := table_get(globals^, k); !ok {
                vm_protect(vm)
                vm_runtime_error(vm, "Attempt to read undefined global '%s'",
                                 value_as_string(k))
            } else {
                ra^ = v
            }
        case .Set_Global:
            key := chunk.constants[ip_get_Bx(read)]
            table_set(vm, globals, key, ra^)
        case .New_Table:
            n_array := fb_decode(cast(u8)read.b)
            n_hash  := fb_decode(cast(u8)read.c)
            ra^ = value_make(table_new(vm, n_array, n_hash))
        case .Get_Table:
            if rb := &frame.window[read.b]; !value_is_table(rb^) {
                index_error(vm, rb)
            } else {
                key := get_rk(read.c, frame)^
                ra^ = table_get(rb.table, key)
            }
        case .Set_Table:
            if !value_is_table(ra^) {
                index_error(vm, ra)
            }
            if k, v := get_rk(read, frame); value_is_nil(k^) {
                index_error(vm, k)
            } else {
                table_set(vm, &ra.table, k^, v^)
            }
        case .Set_Array:
            // Guaranteed to work; only occurs in table constructors
            t := &ra.table
            count  := cast(int)read.b
            offset := cast(int)(read.c - 1) * FIELDS_PER_FLUSH
            for index in 1..=count {
                k := value_make(offset + index)
                v := frame.window[cast(int)read.a + index]
                table_set(vm, t, k, v)
            }
        case .Print:
            for arg, i in frame.window[read.a:read.b] {
                if i != 0 {
                    fmt.print(' ')
                }
                fmt.print(arg)
            }
            fmt.println()
        case .Add: arith_op(vm, number_add, ra, get_rk(read, frame))
        case .Sub: arith_op(vm, number_sub, ra, get_rk(read, frame))
        case .Mul: arith_op(vm, number_mul, ra, get_rk(read, frame))
        case .Div: arith_op(vm, number_div, ra, get_rk(read, frame))
        case .Mod: arith_op(vm, number_mod, ra, get_rk(read, frame))
        case .Pow: arith_op(vm, number_pow, ra, get_rk(read, frame))
        case .Unm:
            if rb := &frame.window[read.b]; !value_is_number(rb^) {
                arith_error(vm, rb, rb)
            } else {
                ra^ = value_make(number_unm(rb.number))
            }
        case .Eq:
            rb, rc := get_rk(read, frame)
            if value_eq(rb^, rc^) != bool(read.a) {
                // Skip the jump which would otherwise load false
                incr_ip(&frame.ip)
            }
        case .Lt:  compare_op(vm, number_lt,  bool(read.a), get_rk(read, frame))
        case .Leq: compare_op(vm, number_leq, bool(read.a), get_rk(read, frame))
        case .Not:
            x := get_rk(read.b, frame)^
            ra^ = value_make(value_is_falsy(x))
        // Add 1 because we want to include Reg[C]
        case .Concat:
            vm_concat(vm, ra, frame.window[read.b:read.c + 1])
        case .Len:
            #partial switch rb := &frame.window[read.b]; rb.type {
            case .String:
                ra^ = value_make(rb.ostring.len)
            case .Table:
                t := &rb.table
                // TODO(2025-04-13): Optimize by separating array from hash!
                count := 0
                for index in 1..=t.count {
                    k := value_make(index)
                    if v := table_get(t^, k); value_is_nil(v) {
                        break
                    }
                    count += 1
                }
                ra^ = value_make(count)
            case:
            vm_protect(vm)
                debug_type_error(vm, rb, "get length of")
            }
        case .Test:
            if !value_is_falsy(ra^) != bool(read.c) {
                incr_ip(&frame.ip)
            }
        case .Test_Set:
            if rb := frame.window[read.b]; !value_is_falsy(rb) != bool(read.c) {
                incr_ip(&frame.ip)
            } else {
                ra^ = rb
            }
        case .Jump:
            offset := ip_get_sBx(read)
            incr_ip(&frame.ip, offset)
        case .For_Prep:
            cond := ptr_offset(ra, 1)
            incr := ptr_offset(ra, 2)
            vm_protect(vm)
            if !value_is_number(ra^) {
                vm_runtime_error(vm, "`for` initial value must be a number")
            } else if !value_is_number(cond^) {
                vm_runtime_error(vm, "`for` condition must be a number")
            } else if !value_is_number(incr^) {
                vm_runtime_error(vm, "`for` increment must be a number")
            }
            // Adjust for the first iteration because the increment will occur
            // right after the condition rather than at the end of the block.
            ra.number = number_sub(ra.number, incr.number)
            loop := ip_get_sBx(read)
            incr_ip(&frame.ip, loop)
        case .For_Loop:
            cond := ptr_offset(ra, 1)
            incr := ptr_offset(ra, 2)
            if number_lt(ra.number, cond.number) {
                ra.number = number_add(ra.number, incr.number)
                body := ip_get_sBx(read)
                incr_ip(&frame.ip, body)
            }
        case .Return:
            // if ip.c != 0 then we have a vararg
            n := cast(int)read.b if read.c == 0 else get_top(vm)
            results := slice.from_ptr(ra, n)
            final   := frame.window[:n]

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
            vm_protect(vm)
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
