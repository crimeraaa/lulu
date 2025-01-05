package lulu

import "core:fmt"
import "core:os"
import "core:io"

LULU_DEBUG :: #config(LULU_DEBUG, true)

@(private="file")
g_vm := &VM{}

main :: proc() {
    vm_init(g_vm)
    defer vm_destroy(g_vm)

    switch len(os.args) {
    case 1: repl(g_vm)
    case 2: run_file(g_vm, os.args[1])
    case:   fmt.eprintfln("Usage: %s [script]", os.args[0])
    }
}

repl :: proc(vm: ^VM) {
    buf: [256]byte

    // Use 'io' so we can use 'io.read' to check for '.Eof'
    stdin := io.to_reader(os.stream_from_handle(os.stdin))
    for {
        fmt.print(">>> ")
        n_read, io_err := io.read(stdin, buf[:])
        if io_err != nil {
            // <C-D> (Unix) or <C-Z><CR> (Windows) is expected.
            if io_err != .EOF {
                fmt.println("Error: ", io_err)
            } else {
                fmt.println()
            }
            break
        }
        vm_interpret(vm, string(buf[:n_read]), "stdin")
    }
}

run_file :: proc(vm: ^VM, file_name: string) {
    data, ok := os.read_entire_file(file_name)
    if !ok {
        fmt.eprintfln("Failed to read file %q.", file_name)
        return
    }
    defer delete(data)

    vm_interpret(vm, string(data[:]), file_name)

}

// === EXPRESSION TESTS ========================================================

@(private="file")
expr1 :: proc(vm: ^VM, chunk: ^Chunk) {
    line := 1
    chunk_init(chunk, "(1.2 + 3.4) / -5.6")
    defer {
        debug_disasm_chunk(chunk^)
        // vm_interpret(vm, chunk)
        chunk_destroy(chunk)
    }

    free_reg: u16

    // 1a.) Prefix: Consume '('.
    // 1b.) Compile all nested expressions. (recursive)
    {
        // 2a.) Prefix: Consume <number-literal>.
        k0 := chunk_add_constant(chunk, 1.2)

        // 2b.) Infix: Consume '+'. We know that the first level expression will
        //      use at least one register. Reserve reg[0], mark reg[1].
        free_reg += 1

        // 2c.) Compile all higher precedence expressions to the right (recursive).
        {
            // 3a.) Prefix: Consume <number-literal>.
            k1 := chunk_add_constant(chunk, 3.4)

            // 3b.) Infix: Peek ')'. Lower precedence, nothing more to do.

            // 3c.) Emit bytecode for 2.)
            //  reg[0] := .const[0] + .const[1]
            //         := 1.2 + 3.4
            //         := 4.6
            chunk_append(chunk, inst_create(.Add, free_reg - 1, rk_as_k(k0), rk_as_k(k1)), line)
        }
        // 2d.) Consume ')'.
    }

    // 4a.) Infix: Consume '/'.
    // 4b.) Compile all higher precedence expressions to the right (recursive)
    {
        // 5a.) Consume '-'. We know that this will require another register,
        //      especially since all unary operators work only on registers.
        free_reg += 1

        // 5b.) Compile all higher precedence expressions to the right (recursive).
        {
            // 6a.) Prefix: Consume <number-literal>.
            k2 := chunk_add_constant(chunk, 5.6)

            // 6b.) Infix: Peek <EOF>. Nothing to do.

            // 6c.) Emit bytecode for 5a.)
            //      reg[1] := .const[2]
            //             := 5.6
            chunk_append(chunk, inst_create_AuBC(.Constant, free_reg - 1, k2), line)
        }
        // 5c.) Emit bytecode for 5a.).
        //      reg[1] := -reg[1]
        //             := -5.6
        chunk_append(chunk, inst_create(.Unm, free_reg - 1, cast(u16)free_reg - 1, 0), line)
    }

    // 4c.) Emit bytecode for 4a.)
    chunk_append(chunk, inst_create(.Div, free_reg - 2, cast(u16)free_reg - 2, cast(u16)free_reg - 1), line)
    emit_return(chunk, free_reg - 2, 1, line)
}

@(private="file")
expr2 :: proc(vm: ^VM, chunk: ^Chunk) {
    chunk_init(chunk, "1 * 2 + 3")
    defer {
        debug_disasm_chunk(chunk^)
        // vm_interpret(vm, chunk)
        chunk_destroy(chunk)
    }
    line := 1
    c0 := chunk_add_constant(chunk, 1.0)
    c1 := chunk_add_constant(chunk, 2.0)

    // reg[0] := .const[0] * .const[1]
    chunk_append(chunk, inst_create(.Mul, 0, rk_as_k(c0), rk_as_k(c1)), line)

    // reg[0] := reg[0] + .const[2]
    c2 := chunk_add_constant(chunk, 3.0)
    chunk_append(chunk, inst_create(.Mul, 0, 0, rk_as_k(c2)), line)

    // return reg[0]..<reg[1]
    emit_return(chunk, 0, 1, line)
}

@(private="file")
expr3 :: proc(vm: ^VM, chunk: ^Chunk) {
    chunk_init(chunk, "1 + 2 * 3")
    defer {
        debug_disasm_chunk(chunk^)
        // vm_interpret(vm, chunk)
        chunk_destroy(chunk)
    }
    line := 1
    free_reg: u16

    // 1a.) Prefix(1): Consume <number-literal>.
    c0 := chunk_add_constant(chunk, 1.0)

    // 1b.) We know that at least the constant will go into reg[0]. If binary,
    //      we will use this as the destination register. reg[1] will be used for
    //      the first higher-precedence recursive expression.
    free_reg += 1

    // 1c.) Infix(1): Consume '+'.
    // 1d.) Compile higher-precedence expressions to the right (recursive).
    {
        // 2a.) Prefix(2): Consume <number-literal>.
        c1 := chunk_add_constant(chunk, 2.0)

        // 2b.) Infix(2): Consume '*'.
        // 2c.) Compile higher-precedence expressions to the right (recursive).
        {
            // 3a.) Prefix(3): Consume <number-literal>.
            c2 := chunk_add_constant(chunk, 3.0)

            // 3b.) Peek <EOF>. Nothing more to do.

            // 3c.) Emit bytecode for 2b.)
            //      reg[0] := .const[1] * .const[2]
            //             := 2.0 * 3.0
            chunk_append(chunk, inst_create(.Mul, free_reg - 1, rk_as_k(c1), rk_as_k(c2)), line)
        }

        // 2d.) Emit bytecode for 1c.)
        //      reg[0] := .const[0] + reg[0]
        //             := 1.0 + 6.0
        //             := 7.0
        chunk_append(chunk, inst_create(.Add, free_reg - 1, rk_as_k(c0), cast(u16)free_reg - 1), line)
    }

    // return reg[0]..<reg[1]
    emit_return(chunk, free_reg - 1, 1, line)
}

@(private="file")
expr4 :: proc(vm: ^VM, chunk: ^Chunk) {
    chunk_init(chunk, "3 - 2 - 1")
    defer {
        debug_disasm_chunk(chunk^)
        // vm_interpret(vm, chunk)
        chunk_destroy(chunk)
    }
    line := 1
    free_reg: u16

    // 1a.) Prefix(1): Consume <number-literal>.
    k0 := chunk_add_constant(chunk, 3.0)

    // We know that at least the constant will go into register 0.
    // If this is a binary expression, this will be the destination.
    free_reg += 1

    // 1b.) Infix(1): Consume '-'.
    // 1c.) Binary: Compile all higher precedence expressions (recursive).
    {
        // 2a.) Prefix: Consume <number-literal>.
        k1 := chunk_add_constant(chunk, 2.0)

        // 2b.) Infix: Peek '-', which is the same precedence.

        // 2c.) Emit bytecode for 1b.)
        //      reg[0] := .const[0] - .const[1]
        //             := 3.0 - 2.0
        //             := 1.0
        chunk_append(chunk, inst_create(.Sub, free_reg - 1, rk_as_k(k0), rk_as_k(k1)), line)
    }

    // 3a.) Infix(2): Consume '-'.
    // 3b.) Binary(2): Compile all higher precedence expressions (recursive).
    {
        // 4a.) Prefix(2): Consume <number-literal>.
        c2 := chunk_add_constant(chunk, 1.0)

        // 4b.) Peek <EOF>. Nothing more to do.
        // 4c.) Emit bytecode for 3a.)
        //      reg[0] := reg[0] - .const[2]
        //             := 1.0  - 1.0
        //             := 0.0
        chunk_append(chunk, inst_create(.Sub, free_reg - 1, cast(u16)free_reg - 1, rk_as_k(c2)), line)
    }

    emit_return(chunk, 0, 1, line)
}

@(private="file")
expr5 :: proc(vm: ^VM, chunk: ^Chunk) {
    // 1 + (2 * 3) - (4 / -5)
    // = 1 + 6 - -0.8
    // = 7 + 0.8
    // = 7.8
    chunk_init(chunk, "1 + 2 * 3 - 4 / -5")
    defer {
        debug_disasm_chunk(chunk^)
        // vm_interpret(vm, chunk)
        chunk_destroy(chunk)
    }

    line := 1
    // 1a) Prefix: Consume <number-literal>
    c0 := chunk_add_constant(chunk, 1.0)
    free_reg: u16

    // 1b.) Infix: Consume '+'
    // 1c.) Compile higher-precedence expressions to right (recursive)
    {
        // 2a.) Prefix: Consume <number-literal>
        c1 := chunk_add_constant(chunk, 2.0)

        // 2b.) Infix: Consume '*'
        // 2c.) Compile higher-precedence expressions to right (recursive)
        {
            // 3a.) Prefix: Consume <number-literal>
            c2 := chunk_add_constant(chunk, 3.0)

            // 3b.). Infix: Peek '+' => Do not compile to right (terminate recursion)
            //      We now know that reg[0] will be used.
            //      Mark reg[1] as free.
            free_reg += 1

            // 3c.) Emit bytecode for 2.)
            //      reg[0] := .const[1] * .const[2]
            //      => 2 * 3 => 6
            chunk_append(chunk, inst_create(.Mul, free_reg - 1, rk_as_k(c1), rk_as_k(c2)), line)
        }
        // 2d.) Emit bytecode for 1.)
        //      reg[0] := .const[0] + reg[0]
        //      => 1 + 6 => 7
        chunk_append(chunk, inst_create(.Add, free_reg - 1, rk_as_k(c0), 0), line)
    }

    // 4a.) Infix: Consume '-'
    // 4b.) Compile higher-precedence expressions to the right (recursive)
    {
        // 5a.) Prefix: Consume <number-literal>
        c3 := chunk_add_constant(chunk, 4.0)

        // 5b.) Infix: Consume '/'. We know that reg[1] will be used for this
        //      operation, so mark reg[2] as free.
        free_reg += 1

        // 5c.) Compile higher-precedence expressions to the right (recursive)
        {
            // 6a.) Prefix: Consume '-'
            // 6b.) Compile higher-precedence expressions to the right (recursive)
            {
                // 7a.) Prefix: Consume <number-literal>
                // 7b.) Emit bytecode for 6.)
                c4 := chunk_add_constant(chunk, 5.0)

                // reg[1] := .const[4] => 5
                chunk_append(chunk, inst_create_AuBC(.Constant, free_reg - 1, c4), line)
            }

            // Emit bytecode for 5.)
            //      reg[1] := -reg[1]
            chunk_append(chunk, inst_create(.Unm, free_reg - 1, cast(u16)free_reg - 1, 0), line)
        }

        // 5e.) Emit bytecode for 4.)
        //      reg[1] := .const[3] / reg[1]
        //  => 4 / -5
        //  => -0.8
        chunk_append(chunk, inst_create(.Div, free_reg - 1, rk_as_k(c3), cast(u16)free_reg - 1), line)
    }

    // 4c.) Emit the last bytecode.
    //  reg[1] := reg[0] - reg[1]
    //  => 7 - -0.8
    //  => 7.8
    chunk_append(chunk, inst_create(.Sub, free_reg - 2, cast(u16)free_reg - 2, cast(u16)free_reg - 1), line)
    emit_return(chunk, free_reg - 2, 1, line)
}

@(private="file")
emit_return :: proc(chunk: ^Chunk, #any_int i_reg, n_ret: u16, line: int) {
    chunk_append(chunk, inst_create(.Return, i_reg, n_ret, 0), line)
}
