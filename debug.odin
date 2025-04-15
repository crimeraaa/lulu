#+private
package lulu

import "core:fmt"

debug_dump_chunk :: proc(chunk: Chunk) {
    fmt.printfln("=== STACK USAGE ===\n%i", chunk.stack_used)

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
    for inst, index in chunk.code[:chunk.pc] {
        debug_dump_instruction(chunk, inst, index)
    }
}

debug_dump_instruction :: proc(chunk: Chunk, inst: Instruction, index: int) {
    // unary negation, not and length never operate on constant indexes.
    unary :: proc(op: string, inst: Instruction) {
        print_AB(inst)
        fmt.printfln("reg[%i] := %sreg[%i]", inst.a, op, inst.b)
    }

    binary :: proc(op: string, inst: Instruction) {
        b_where, b_index := get_rk(inst.b)
        c_where, c_index := get_rk(inst.c)
        print_ABC(inst)
        fmt.printfln("reg[%i] := %s[%i] %s %s[%i]", inst.a, b_where, b_index, op, c_where, c_index)
    }

    get_rk :: proc(b_or_c: u16) -> (location: string, index: int) {
        is_k := reg_is_k(b_or_c)

        location = ".const" if is_k else "reg"
        index    = cast(int)(reg_get_k(b_or_c) if is_k else b_or_c)
        return location, index
    }

    print_AB :: proc(inst: Instruction) {
        fmt.printf("% 8i % 4i % 4s ; ", inst.a, inst.b, " ")
    }

    print_ABx :: proc(inst: Instruction) {
        fmt.printf("% 8i % 4i % 4s ; ", inst.a, inst_get_Bx(inst), " ")
    }

    print_ABC :: proc(inst: Instruction) {
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
    case .Move:
        print_AB(inst)
        fmt.printfln("reg[%i] := reg[%i]", inst.a, inst.b)
    case .Load_Constant:
        bc := inst_get_Bx(inst)
        print_ABx(inst)
        fmt.printf("reg[%i] := .const[%i] => ", inst.a, bc)
        value_print(chunk.constants[bc], .Debug)
    case .Load_Nil:
        print_AB(inst)
        fmt.printfln("reg[%i..=%i] := nil", inst.a, inst.b)
    case .Load_Boolean:
        print_ABC(inst)
        fmt.printf("reg[%i] := %v", inst.a, inst.b == 1)
        if inst.c == 1 {
            fmt.println("; pc++")
        } else {
            fmt.println()
        }
    case .Get_Global, .Set_Global:
        print_ABx(inst)
        key := chunk.constants[inst_get_Bx(inst)]
        assert(value_is_string(key))
        identifier := ostring_to_string(key.ostring)
        if inst.op == .Get_Global {
            fmt.printfln("reg[%i] := _G[%q]", inst.a, identifier)
        } else {
            fmt.printfln("_G[%q] := reg[%i]",  identifier, inst.a)
        }
    case .New_Table:
        print_ABC(inst)
        fmt.printfln("reg[%i] = {{}} ; #array = %i, #hash = %i", inst.a, inst.b, inst.c)
    case .Get_Table:
        print_ABC(inst)
        s, c := get_rk(inst.c)
        fmt.printfln("reg[%i] := reg[%i][%s[%i]]", inst.a, inst.b, s, c)
    case .Set_Table:
        b_loc, b := get_rk(inst.b)
        c_loc, c := get_rk(inst.c)
        print_ABC(inst)
        fmt.printfln("reg[%i][%s[%i]] = %s[%i]", inst.a, b_loc, b, c_loc, c)
    case .Set_Array:
        print_ABx(inst)
        fmt.printfln("reg[%i][1:%i] = ...", inst.a, inst_get_Bx(inst))
    case .Print:
        print_AB(inst)
        fmt.printfln("print(reg[%i..<%i])", inst.a, inst.b)
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
        print_ABC(inst)
        fmt.printfln("reg[%i] := concat(reg[%i..=%i])", inst.a, inst.b, inst.c)
    case .Len:
        unary("#", inst)
    case .Return:
        start := inst.a
        stop  := inst.b - 1 if inst.b != 0 else start
        print_AB(inst)
        fmt.printfln("return reg[%i..<%i]", start, stop)
    }
}
