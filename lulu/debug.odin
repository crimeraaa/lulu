#+private
package lulu

import "core:fmt"
import "core:math"

@(init)
debug_init_formatters :: proc() {
    fmt.set_user_formatters(new(map[typeid]fmt.User_Formatter))
    fmt.register_user_formatter(^OString, ostring_formatter)
    fmt.register_user_formatter(Local, local_formatter)
    fmt.register_user_formatter(Value, value_formatter)
}

debug_dump_chunk :: proc(chunk: ^Chunk) {
    fmt.printfln("=== STACK USAGE ===\n%i", chunk.stack_used)

    fmt.println("=== DISASSEMBLY: BEGIN ===")
    defer fmt.println("\n=== DISASSEMBLY: END ===")

    fmt.printfln("\n.name\n%q", chunk.source)

    if n := len(chunk.locals); n > 0 {
        fmt.println("\n.local:")
        left_pad := math.count_digits_of_base(n, 10)
        for local, index in chunk.locals {
            fmt.printfln("[%0*i] %q ; local %v", left_pad, index, local.ident,
                local)
        }
    }

    if n := len(chunk.constants); n > 0 {
        fmt.println("\n.const:")
        left_pad := math.count_digits_of_base(n, 10)
        for constant, index in chunk.constants {
            fmt.printfln("[%0*i] %d", left_pad, index, constant)
        }
    }

    fmt.println("\n.code")
    left_pad := math.count_digits_of_base(chunk.pc, 10)
    for inst, index in chunk.code[:chunk.pc] {
        debug_dump_instruction(chunk, inst, index, left_pad)
    }
}

debug_dump_instruction :: proc(chunk: ^Chunk, ip: Instruction, index: int, left_pad := 4) {
    Print_Info :: struct {
        chunk: ^Chunk,
        pc:     int,
        ip:     Instruction,
    }

    unary :: proc(info: Print_Info, op: string) {
        print_reg(info, info.ip.a, " := %s", op)
        print_reg(info, info.ip.b)
    }

    binary :: proc(info: Print_Info, op: string) {
        print_reg(info, info.ip.a, " := ")
        print_reg(info, info.ip.b, " %s ", op)
        print_reg(info, info.ip.c)
    }

    compare :: proc(info: Print_Info, op: string) {
        print_reg(info, info.ip.a, " := ")
        print_reg(info, info.ip.b, " %s ", op)
        print_reg(info, info.ip.c)
    }

    print_reg :: proc(info: Print_Info, reg: u16, format := "", args: ..any) {
        defer if format != "" {
            fmt.printf(format, ..args)
        }
        chunk := info.chunk
        if reg_is_k(reg) {
            index := cast(int)reg_get_k(reg)
            fmt.printf("%d", chunk.constants[index])
            return
        }
        if local, ok := chunk_get_local(chunk, cast(int)reg + 1, info.pc); ok {
            fmt.printf("local %s", local_to_string(local))
        } else {
            fmt.printf("Reg(%i)", reg)
        }
    }

    fmt.printf("[%0*i] ", left_pad, index)
    if line := chunk.line[index]; index > 0 && line == chunk.line[index - 1] {
        fmt.print("   | ")
    } else {
        fmt.printf("% 4i ", line)
    }

    fmt.printf("%-14v ", ip.op)
    defer fmt.println()

    switch info := opcode_info[ip.op]; info.type {
    case .Separate:
        fmt.printf("% 8i % 4i ", ip.a, ip.b)
        if info.c == .Unused {
            fmt.printf("% 4s", " ")
        } else {
            fmt.printf("% 4i", ip.c)
        }
        fmt.printf(" ; ")
    case .Signed_Bx:
        fmt.printf("% 8i % 4i % 4s ; ", ip.a, ip_get_sBx(ip), " ")
    case .Unsigned_Bx:
        fmt.printf("% 8i % 4i % 4s ; ", ip.a, ip_get_Bx(ip), " ")
    case:
        unreachable("Bad opcode info %v", info)
    }

    info := Print_Info{chunk = chunk, pc = index, ip = ip}
    switch (ip.op) {
    case .Move:
        print_reg(info, ip.a, " := ")
        print_reg(info, ip.b)
    case .Load_Constant:
        bc := ip_get_Bx(ip)
        print_reg(info, ip.a, " := %d", chunk.constants[bc])
    case .Load_Nil:
        fmt.printf("Reg(i) := nil for %i <= i <= %i", ip.a, ip.b)
    case .Load_Boolean:
        print_reg(info, ip.a, " := %v", ip.b == 1)
        if ip.c == 1 {
            fmt.print("; goto .code[%i]", index + 2)
        }
    case .Get_Global:
        key := chunk.constants[ip_get_Bx(ip)]
        print_reg(info, ip.a, " := _G.%s", value_to_string(key))
    case .Get_Table:
        print_reg(info, ip.a, " := ")
        print_reg(info, ip.b, "[")
        print_reg(info, ip.c, "]")
    case .Set_Global:
        key := chunk.constants[ip_get_Bx(ip)]
        fmt.printf("_G.%s := ",  value_to_string(key))
        print_reg(info, ip.a)
    case .Set_Table:
        print_reg(info, ip.a, "[")
        print_reg(info, ip.b, "] = ")
        print_reg(info, ip.c)
    case .New_Table:
        print_reg(info, ip.a, " = {{}} ; #array=%i, #hash=%i",
                  fb_to_int(cast(u8)ip.b), fb_to_int(cast(u8)ip.c))
    case .Set_Array:
        print_reg(info, ip.a, "[%i+i] = Reg(%i+i) for 1 <= i <= %i",
                  cast(int)(ip.c - 1) * FIELDS_PER_FLUSH, ip.a, ip.b)
    case .Print:
        fmt.printf("print(Reg(i), \\t) for %i <= i < %i", ip.a, ip.b)
    case .Add: binary(info, "+")
    case .Sub: binary(info, "-")
    case .Mul: binary(info, "*")
    case .Div: binary(info, "/")
    case .Mod: binary(info, "%")
    case .Pow: binary(info, "^")
    case .Unm: unary(info,  "-")
    case .Eq:  compare(info, "==")
    case .Neq: compare(info, "~=")
    case .Lt:  compare(info, "<")
    case .Leq: compare(info, "<=")
    case .Not: unary(info,  "not ")
    case .Concat:
        print_reg(info, ip.a, " := concat(Reg(%i..=%i))", ip.b, ip.c)
    case .Len:
        unary(info, "#")
    case .Test:
        fmt.print("if Bool(")
        print_reg(info, ip.a, ") == %v then goto .code[%i]", bool(ip.c), index + 2)
    case .Jump:
        fmt.printf("goto .code[%i]", index + 1 + ip_get_sBx(ip))
    case .Return:
        reg       := cast(int)ip.a
        n_results := cast(int)ip.b
        if ip.c == 0 {
            fmt.printf("return Reg(%i..<%i)", reg, reg + n_results)
        } else {
            fmt.printf("return Reg(%i..=%i)", reg, chunk.stack_used);
        }
    case:
        unreachable("Unknown opcode %v", ip.op)
    }
}


debug_type_error :: proc(vm: ^VM, culprit: ^Value, action: string) -> ! {
    type_name := value_type_name(culprit^)

    // Culprit is in the active stack frame, thus may be a variable?
    if reg, in_stack := ptr_index_safe(culprit, vm.base[:get_top(vm)]); in_stack {
        chunk := vm.chunk

        // Inline implementation `ldebug.c:currentpc()`.
        // `pc` always points to instruction AFTER current, so culprit is
        // at the index of `pc - 1`
        pc := ptr_index(vm.pc, chunk.code) - 1

        // Culprit is a local variable, global variable, or a field?
        if ident, scope, is_var := debug_get_variable(chunk, pc, reg); is_var {
            vm_runtime_error(vm, "Attempt to %s %s %q (a %s value)",
                         action, scope, ident, type_name)
        }
    }
    vm_runtime_error(vm, "Attempt to %s a %s value", action, type_name)
}


/*
**Overview**
-   Attempts to resolve the global/local/field name of the stack index `reg` at
    the desired instruction index `pc`.

**Analogous to**
-   `ldebug.c:getobjname(lua_State *L, CallInfo *ci, int stackpos, const char **name)`
 */
debug_get_variable :: proc(chunk: ^Chunk, pc, reg: int) -> (ident, scope: string, ok: bool) {
    /*
    **Analogous to**
    -   `ldebug.c:kname(Proto *p, int c)` in Lua 5.1.5.
     */
    constant_ident :: proc(chunk: ^Chunk, reg: u16) -> string {
        if reg_is_k(reg) {
            if key := chunk.constants[reg_get_k(reg)]; value_is_string(key) {
                return value_to_string(key)
            }
        }
        // Either non-constant or non-string-constant, can't determine its name
        return "?"
    }

    if local, found := chunk_get_local(chunk, reg + 1, pc); found {
        return local_to_string(local), "local", true
    }
    ip := debug_symbolic_execution(chunk, pc, cast(u16)reg) or_return
    #partial switch ip.op {
    case .Move:
        // Moving from Reg(B) to Reg(A)?
        if ip.b < ip.a {
            // Get the name for Reg(B).
            // TODO(2025-04-22): Find out how this path is reached!
            return debug_get_variable(chunk, pc, cast(int)ip.b)
        }
    case .Get_Global:
        bx     := ip_get_Bx(ip)
        global := chunk.constants[bx]
        return value_to_string(global), "global", true
    case .Get_Table:
        return constant_ident(chunk, ip.c), "field", true
    case:
        break;
    }
    return "", "", false
}


/*
**Analogous to**
-   `ldebug.c:symbexec(const Proto *pt, int lastpc, int reg)` in Lua 5.1.5.
 */
debug_symbolic_execution :: proc(chunk: ^Chunk, lastpc: int, reg: u16) -> (i: Instruction, ok: bool) {
    /*
    **Overview**
    -   Checks if `reg` is in range of the stack frame.

    **Assumptions**
    -   Since `reg` is unsigned, it can never be less than 0.
     */
    check_reg :: proc(chunk: ^Chunk, reg: u16) -> bool {
        return cast(int)reg < chunk.stack_used
    }

    /*
    **Overview**
    -   Wrapped implementation of the first `switch` statement in `symbexec()`.
     */
    check_arg_info :: proc(chunk: ^Chunk, ip: Instruction) -> bool {
        info := opcode_info[ip.op]
        switch info.type {
        case .Separate:
            check_arg_mode(chunk, ip.b, info.b) or_return
            check_arg_mode(chunk, ip.c, info.c) or_return
            return true
        case .Unsigned_Bx:
            index := ip_get_Bx(ip)
            if info.b == .Reg_Const {
                return cast(int)index < len(chunk.constants)
            }
            return true
        case .Signed_Bx:
            // Based on the C source, this one is quite tricky!
            panic("Signed_Bx not yet implemented")
        case:
            unreachable("Unknown %[0]t %[0]v", info.type)
        }
    }

    check_arg_mode :: proc(chunk: ^Chunk, reg: u16, type: OpCode_Arg_Type) -> bool {
        switch type {
        case .Unused:
            return reg == 0
        case .Used:
            // Not enough information to verify how we should use this
            return true
        case .Reg_Const:
            if reg_is_k(reg) {
                return cast(int)reg_get_k(reg) < len(chunk.constants)
            }
            fallthrough
        case .Reg_Jump:
            // Register/jump is in range of the bytecode?
            return cast(int)reg < chunk.stack_used
        case:
            unreachable("")
        }
    }
    // Point to the return 0 0 0 instruction; it's always a safe bet
    index := len(chunk.code) - 1
    for ip, ip_index in chunk.code[:lastpc] {
        check_reg(chunk, ip.a) or_return
        check_arg_info(chunk, ip) or_return

        // Need to change register A?
        if opcode_info[ip.op].a {
            if ip.a == reg {
                index = ip_index
            }
        }

        #partial switch ip.op {
        case .Load_Boolean:
            // Have a jump?
            if ip.c == 1 {
                panic("Boolean with jump not yet supported")
            }
        case .Load_Nil:
            // Set registers from `a` to `b`
            if ip.a <= reg && reg <= ip.b {
                index = ip_index
            }
        case .Get_Global, .Set_Global:
            value_is_string(chunk.constants[ip_get_Bx(ip)]) or_return
        case .Concat:
            // Require at least 2 operands
            (ip.b < ip.c) or_return
        case .Return:
            // If not in vararg return, ensure known registers are in range
            if ip.c == 0 {
                check_reg(chunk, ip.a + ip.b) or_return
            }
        case .Set_Array:
            if ip.b > 0 {
                check_reg(chunk, ip.a + ip.b) or_return
            }
            if ip.c == 0 {
                panic("Set_Array with large C not yet supported")
            }
        }
    }

    return chunk.code[index], true

}
