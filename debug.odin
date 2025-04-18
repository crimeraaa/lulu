#+private
package lulu

import "core:fmt"

@(private="file",init)
init_formatters :: proc() {
    fmt.set_user_formatters(new(map[typeid]fmt.User_Formatter))
    err := fmt.register_user_formatter(^OString, ostring_formatter)
    assert(err == nil)

    err = fmt.register_user_formatter(Local, local_formatter)
    assert(err == nil)

    err = fmt.register_user_formatter(Value, value_formatter)
    assert(err == nil)
}

debug_dump_chunk :: proc(chunk: ^Chunk) {
    fmt.printfln("=== STACK USAGE ===\n%i", chunk.stack_used)

    fmt.println("=== DISASSEMBLY: BEGIN ===")
    defer fmt.println("\n=== DISASSEMBLY: END ===")

    fmt.printfln("\n.name\n%q", chunk.source)

    if dyarray_len(chunk.locals) > 0 {
        fmt.println("\n.local:")
        for local, index in dyarray_slice(&chunk.locals) {
            fmt.printfln("[%04i] %q", index, local.ident)
        }
    }

    if dyarray_len(chunk.constants) > 0 {
        fmt.println("\n.const:")
        for constant, index in dyarray_slice(&chunk.constants) {
            fmt.printf("[%04i] ", index)
            value_print(constant, .Debug)
            fmt.println()
        }
    }

    fmt.println("\n.code")
    for inst, index in dyarray_slice(&chunk.code) {
        debug_dump_instruction(chunk, inst, index)
    }
}

debug_dump_instruction :: proc(chunk: ^Chunk, inst: Instruction, index: int) {
    unary :: proc(chunk: ^Chunk, op: string, inst: Instruction) {
        print_AB(inst)
        print_reg(chunk, inst.a)
        fmt.printf(" := %s", op)
        print_reg(chunk, inst.b)
    }

    binary :: proc(chunk: ^Chunk, op: string, inst: Instruction) {
        print_ABC(inst)
        print_reg(chunk, inst.a, " := ")
        print_reg(chunk, inst.b, " %s ", op)
        print_reg(chunk, inst.c)
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

    print_reg :: proc(chunk: ^Chunk, reg: u16, format := "", args: ..any) {
        defer if format != "" {
            fmt.printf(format, ..args)
        }
        if reg_is_k(reg) {
            index := cast(int)reg_get_k(reg)
            value_print(dyarray_get(chunk.constants, index), .Debug)
            return
        }
        if local, ok := dyarray_get_safe(chunk.locals, cast(int)reg); ok {
            // see `chunk.odin:local_formatter()`
            fmt.print("local", local)
        } else {
            fmt.printf("reg[%i]", reg)
        }
    }

    fmt.printf("[%04i] ", index)
    line := dyarray_get(chunk.line, index)
    if index > 0 && line == dyarray_get(chunk.line, index - 1) {
        fmt.print("   | ")
    } else {
        fmt.printf("% 4i ", line)
    }

    fmt.printf("%-16v ", inst.op)
    defer fmt.println()
    switch (inst.op) {
    case .Move:
        print_AB(inst)
        print_reg(chunk, inst.a, " := ")
        print_reg(chunk, inst.b)
    case .Load_Constant:
        bc := inst_get_Bx(inst)
        print_ABx(inst)
        print_reg(chunk, inst.a, " := ")
        value_print(dyarray_get(chunk.constants, cast(int)bc), .Debug)
    case .Load_Nil:
        print_AB(inst)
        fmt.printf("reg[%i..=%i] := nil", inst.a, inst.b)
    case .Load_Boolean:
        print_ABC(inst)
        print_reg(chunk, inst.a, " := ")
        fmt.printf(" := %v", inst.b == 1)
        if inst.c == 1 {
            fmt.print("; pc++")
        }
    case .Get_Global, .Set_Global:
        print_ABx(inst)
        key := dyarray_get(chunk.constants, cast(int)inst_get_Bx(inst))
        assert(value_is_string(key))
        if inst.op == .Get_Global {
            print_reg(chunk, inst.a, " := _G.%s", key.ostring)
        } else {
            fmt.printf("_G.%s := ",  key.ostring)
            print_reg(chunk, inst.a)
        }
    case .New_Table:
        print_ABC(inst)
        print_reg(chunk, inst.a, " = {{}} ; #array=%i, #hash=%i",
                  fb_to_int(cast(u8)inst.b), fb_to_int(cast(u8)inst.c))
    case .Get_Table:
        print_ABC(inst)
        print_reg(chunk, inst.a, " := ")
        print_reg(chunk, inst.b, "[")
        print_reg(chunk, inst.c, "]")
    case .Set_Table:
        print_ABC(inst)
        print_reg(chunk, inst.a, "[")
        print_reg(chunk, inst.b, "] = ")
        print_reg(chunk, inst.c)
    case .Set_Array:
        print_ABC(inst)
        assert(inst.b != 0 && inst.c != 0, "Impossible condition reached")
        print_reg(chunk, inst.a, "[%i+i] = reg(%i+i) for 1 <= i <= %i",
                 (inst.c - 1) * FIELDS_PER_FLUSH, inst.a, inst.b)
    case .Print:
        print_AB(inst)
        fmt.printf("print(reg[%i..<%i])", inst.a, inst.b)
    case .Add: binary(chunk, "+", inst)
    case .Sub: binary(chunk, "-", inst)
    case .Mul: binary(chunk, "*", inst)
    case .Div: binary(chunk, "/", inst)
    case .Mod: binary(chunk, "%", inst)
    case .Pow: binary(chunk, "^", inst)
    case .Unm: unary(chunk, "-", inst)
    case .Eq:  binary(chunk, "==", inst)
    case .Neq: binary(chunk, "~=", inst)
    case .Lt:  binary(chunk, "<", inst)
    case .Gt:  binary(chunk, ">", inst)
    case .Leq: binary(chunk, "<=", inst)
    case .Geq: binary(chunk, ">=", inst)
    case .Not: unary(chunk,"not ", inst)
    case .Concat:
        print_ABC(inst)
        print_reg(chunk, inst.a, " := concat(reg[%i..=%i])", inst.b, inst.c)
    case .Len:
        unary(chunk, "#", inst)
    case .Return:
        print_ABC(inst)
        reg  := cast(int)inst.a
        nret := cast(int)inst.b
        if inst.c == 0 {
            fmt.printf("return reg[%i..<%i]", reg, reg + nret)
        } else {
            fmt.printf("return reg[%i..(top)]", reg);
        }
    }
}
