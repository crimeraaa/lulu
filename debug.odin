#+private
package lulu

import "core:fmt"

debug_dump_chunk :: proc(chunk: Chunk) {
    fmt.println("=== DISASSEMBLY: BEGIN ===")
    defer {
        fmt.println("\n=== DISASSEMBLY: END ===")
    }

    fmt.printfln("\n.name\n%q", chunk.source)
    fmt.println("\n.const:")
    for constant, index in chunk.constants {
        fmt.printf("[%04i] ", index)
        value_print(constant, .Debug)
    }

    fmt.println("\n.code")
    for inst, index in chunk.code {
        debug_dump_instruction(chunk, inst, index)
    }
}

debug_dump_instruction :: proc(chunk: Chunk, inst: Instruction, index: int) {
    // unary negation, not and length never operate on constant indexes.
    unary :: #force_inline proc($op: string, inst: Instruction) {
        b_where, b_index := get_rk(inst.b)
        print_args2(inst)
        fmt.printfln("reg[%i] := %s%s[%i]", inst.a, op, b_where, b_index)
    }

    binary :: #force_inline proc($op: string, inst: Instruction) {
        b_where, b_index := get_rk(inst.b)
        c_where, c_index := get_rk(inst.c)
        print_args3(inst)
        fmt.printfln("reg[%i] := %s[%i] %s %s[%i]", inst.a, b_where, b_index, op, c_where, c_index)
    }

    get_rk :: #force_inline proc(b_or_c: u16) -> (location: string, index: int) {
        is_k := rk_is_k(b_or_c)

        location = ".const" if is_k else "reg"
        index    = cast(int)(rk_get_k(b_or_c) if is_k else b_or_c)
        return location, index
    }

    print_args2 :: proc(inst: Instruction) {
        fmt.printf("% 8i % 4i % 4s ; ", inst.a, inst.b, " ")
    }

    print_AuBC :: proc(inst: Instruction) {
        fmt.printf("% 8i % 4i % 4s ; ", inst.a, inst_get_Bx(inst), " ")
    }

    print_args3 :: proc(inst: Instruction) {
        fmt.printf("% 8i % 4i % 4i ; ", inst.a, inst.b, inst.c)
    }

    fmt.printf("[%04i] ", index)
    if index > 0 && chunk.line[index] == chunk.line[index - 1] {
        fmt.print("   | ")
    } else {
        fmt.printf("% 4i ", chunk.line[index])
    }
    fmt.printf("%-16v ", inst.op)
    switch (inst.op) {
    case .Load_Constant:
        a, bc := inst.a, inst_get_Bx(inst)
        print_AuBC(inst)
        fmt.printf("reg[%i] := .const[%i] => ", a, bc)
        value_print(chunk.constants[bc], .Debug)
    case .Load_Nil:
        print_args2(inst)
        fmt.printfln("reg[%i]..=reg[%v] := nil", inst.a, inst.b)
    case .Load_Boolean:
        print_args3(inst)
        fmt.printf("reg[%i] := %s", inst.a, "true" if inst.b == 1 else "false")
        if inst.c == 1 {
            fmt.println("; ip++")
        } else {
            fmt.println()
        }
    case .Print:
        print_args2(inst)
        fmt.printfln("print(reg[%i]..=reg[%i])", inst.a, inst.b)
    case .Add: binary("+", inst)
    case .Sub: binary("-", inst)
    case .Mul: binary("*", inst)
    case .Div: binary("/", inst)
    case .Mod: binary("%", inst)
    case .Pow: binary("^", inst)
    case .Unm: unary("-", inst)
    case .Eq:  binary("==", inst)
    case .Neq: binary("~=", inst)
    case .Lt:  binary("<", inst)
    case .Gt:  binary(">", inst)
    case .Leq: binary("<=", inst)
    case .Geq: binary(">=", inst)
    case .Not: unary("not ", inst)
    case .Concat:
        print_args3(inst)
        fmt.printfln("reg[%i] := reg[%i] .. reg[%i]", inst.a, inst.b, inst.c)
    case .Return:
        start := inst.a
        stop  := inst.b - 1 if inst.b != 0 else start
        print_args2(inst)
        fmt.printfln("return reg[%i]..<reg[%v]", start, stop)
    }
}
