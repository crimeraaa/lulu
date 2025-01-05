#+private
package lulu

import "core:fmt"

debug_disasm_chunk :: proc(chunk: Chunk) {
    fmt.println("=== DISASSEMBLY: BEGIN ===")
    defer {
        fmt.println("\n=== DISASSEMBLY: END ===")
    }

    fmt.printfln("\n.name\n%q", chunk.source)
    fmt.println("\n.const:")
    for constant, index in chunk.constants {
        fmt.printfln("[%04i] %v", index, constant)
    }

    fmt.println("\n.code")
    for inst, index in chunk.code {
        debug_disasm_inst(chunk, inst, index)
    }
}

debug_disasm_inst :: proc(chunk: Chunk, inst: Instruction, index: int) {
    // unary negation, not and length never operate on constant indexes.
    unary :: proc($op: string, inst: Instruction) {
        print_args2(inst)
        fmt.printfln("reg[%i] := reg%s[%i]", inst.a, op, inst.b)
    }

    binary :: proc($op: string, inst: Instruction) {
        b_where, b_index := get_rk(inst.b)
        c_where, c_index := get_rk(inst.c)
        print_args3(inst)
        fmt.printfln("reg[%i] := %s[%i] %s %s[%i]", inst.a, b_where, b_index, op, c_where, c_index)
    }

    get_rk :: proc(b_or_c: u16) -> (location: string, index: int) {
        is_k := rk_is_k(b_or_c)

        location = ".const" if is_k else "reg"
        index    = cast(int)(rk_get_k(b_or_c) if is_k else b_or_c)
        return location, index
    }

    print_args2 :: proc(inst: Instruction) {
        fmt.printf("% 8i % 4i % 4s ; ", inst.a, inst.b, " ")
    }

    print_AuBC :: proc(inst: Instruction) {
        fmt.printf("% 8i % 4i % 4s ; ", inst.a, inst_get_uBC(inst), " ")
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
    fmt.printf("%v.%-8v ", typeid_of(type_of(inst.op)), inst.op)
    switch (inst.op) {
    case .Constant:
        a, bc := inst.a, inst_get_uBC(inst)
        print_AuBC(inst)
        fmt.printf("reg[%i] := .const[%i] => ", a, bc)
        value_print(chunk.constants[bc])
    case .Add: binary("+", inst)
    case .Sub: binary("-", inst)
    case .Mul: binary("*", inst)
    case .Div: binary("/", inst)
    case .Mod: binary("%", inst)
    case .Pow: binary("^", inst)
    case .Unm: unary("-", inst)
    case .Return:
        a := inst.a
        b := inst.b
        print_args2(inst)
        fmt.printfln("return reg[%i]..<reg[%v]", a, u16(a) + b)
    }
}
