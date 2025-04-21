#+private
package lulu

import "core:fmt"
import "core:math"

@(private="file", init)
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

    if n := len(chunk.locals); n > 0 {
        fmt.println("\n.local:")
        n_digits := math.count_digits_of_base(n, 10)
        for local, index in chunk.locals {
            fmt.printfln("[%0*i] %q ; local %v", n_digits, index, local.ident,
                local)
        }
    }

    if n := len(chunk.constants); n > 0 {
        fmt.println("\n.const:")
        n_digits := math.count_digits_of_base(n, 10)
        for constant, index in chunk.constants {
            fmt.printf("[%0*i] ", n_digits, index)
            value_print(constant, .Debug)
            fmt.println()
        }
    }

    fmt.println("\n.code")
    left_pad := math.count_digits_of_base(chunk.pc, 10)
    for inst, index in chunk.code[:chunk.pc] {
        debug_dump_instruction(chunk, inst, index, left_pad)
    }
}

debug_dump_instruction :: proc(chunk: ^Chunk, inst: Instruction, index: int, left_pad := 4) {
    Print_Info :: struct {
        chunk: ^Chunk,
        pc:     int,
    }

    unary :: proc(info: Print_Info, op: string, inst: Instruction) {
        print_AB(inst)
        print_reg(info, inst.a)
        fmt.printf(" := %s", op)
        print_reg(info, inst.b)
    }

    binary :: proc(info: Print_Info, op: string, inst: Instruction) {
        print_ABC(inst)
        print_reg(info, inst.a, " := ")
        print_reg(info, inst.b, " %s ", op)
        print_reg(info, inst.c)
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

    print_reg :: proc(info: Print_Info, reg: u16, format := "", args: ..any) {
        defer if format != "" {
            fmt.printf(format, ..args)
        }
        chunk := info.chunk
        if reg_is_k(reg) {
            index := cast(int)reg_get_k(reg)
            value_print(chunk.constants[index], .Debug)
            return
        }
        if local, ok := chunk_get_local(chunk, cast(int)reg + 1, info.pc); ok {
            // see `chunk.odin:local_formatter()`
            fmt.print("local", local)
        } else {
            fmt.printf("reg[%i]", reg)
        }
    }

    fmt.printf("[%0*i] ", left_pad, index)
    if line := chunk.line[index]; index > 0 && line == chunk.line[index - 1] {
        fmt.print("   | ")
    } else {
        fmt.printf("% 4i ", line)
    }

    fmt.printf("%-14v ", inst.op)
    defer fmt.println()

    info := Print_Info{chunk = chunk, pc = index}
    switch (inst.op) {
    case .Move:
        print_AB(inst)
        print_reg(info, inst.a, " := ")
        print_reg(info, inst.b)
    case .Load_Constant:
        bc := inst_get_Bx(inst)
        print_ABx(inst)
        print_reg(info, inst.a, " := ")
        value_print(chunk.constants[bc], .Debug)
    case .Load_Nil:
        print_AB(inst)
        fmt.printf("reg[%i..=%i] := nil", inst.a, inst.b)
    case .Load_Boolean:
        print_ABC(inst)
        print_reg(info, inst.a, " := ")
        fmt.printf(" := %v", inst.b == 1)
        if inst.c == 1 {
            fmt.print("; pc++")
        }
    case .Get_Global, .Set_Global:
        print_ABx(inst)
        key := chunk.constants[inst_get_Bx(inst)]
        assert(value_is_string(key))
        if inst.op == .Get_Global {
            print_reg(info, inst.a, " := _G.%s", key.ostring)
        } else {
            fmt.printf("_G.%s := ",  key.ostring)
            print_reg(info, inst.a)
        }
    case .New_Table:
        print_ABC(inst)
        print_reg(info, inst.a, " = {{}} ; #array=%i, #hash=%i",
                  fb_to_int(cast(u8)inst.b), fb_to_int(cast(u8)inst.c))
    case .Get_Table:
        print_ABC(inst)
        print_reg(info, inst.a, " := ")
        print_reg(info, inst.b, "[")
        print_reg(info, inst.c, "]")
    case .Set_Table:
        print_ABC(inst)
        print_reg(info, inst.a, "[")
        print_reg(info, inst.b, "] = ")
        print_reg(info, inst.c)
    case .Set_Array:
        print_ABC(inst)
        assert(inst.b != 0 && inst.c != 0, "Impossible condition reached")
        print_reg(info, inst.a, "[%i+i] = reg(%i+i) for 1 <= i <= %i",
                 (inst.c - 1) * FIELDS_PER_FLUSH, inst.a, inst.b)
    case .Print:
        print_AB(inst)
        fmt.printf("print(reg[%i..<%i])", inst.a, inst.b)
    case .Add: binary(info, "+", inst)
    case .Sub: binary(info, "-", inst)
    case .Mul: binary(info, "*", inst)
    case .Div: binary(info, "/", inst)
    case .Mod: binary(info, "%", inst)
    case .Pow: binary(info, "^", inst)
    case .Unm: unary(info,  "-", inst)
    case .Eq:  binary(info, "==", inst)
    case .Neq: binary(info, "~=", inst)
    case .Lt:  binary(info, "<", inst)
    case .Gt:  binary(info, ">", inst)
    case .Leq: binary(info, "<=", inst)
    case .Geq: binary(info, ">=", inst)
    case .Not: unary(info,  "not ", inst)
    case .Concat:
        print_ABC(inst)
        print_reg(info, inst.a, " := concat(reg[%i..=%i])", inst.b, inst.c)
    case .Len:
        unary(info, "#", inst)
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
