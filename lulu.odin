package lulu

import "core:fmt"
import "core:os"

LULU_DEBUG :: #config(LULU_DEBUG, true)

main :: proc() {
    vm: VM
    chunk: Chunk
    expr1(&vm, &chunk)
}

@(private="file")
expr1 :: proc(vm: ^VM, chunk: ^Chunk) {
    line := 1
    vm_init(vm)
    chunk_init(chunk, "(1.2 + 3.4) / -5.6")
    defer {
        chunk_destroy(chunk)
        vm_destroy(vm)
    }
    
    base_reg, free_reg: u8

    x := cast(u8)chunk_add_constant(chunk, 1.2) // Kst[0]
    y := cast(u8)chunk_add_constant(chunk, 3.4) // Kst[1]
    z := chunk_add_constant(chunk, 5.6) // Kst[2]
    fmt.println("x, y, z :=", x, y, z)
    // Reg[0] = Kst[0] + Kst[1]; Top = Reg[1]
    chunk_append(chunk, inst_create(.Add, free_reg, rk_as_k(x), rk_as_k(y)) , line)
    free_reg += 1
    
    // Reg[1] = Kst[2]; Top = Reg[2]
    chunk_append(chunk, inst_create_AuBC(.Constant, free_reg, z), line)
    free_reg += 1
    
    // Reg[1] = -Reg[1]
    chunk_append(chunk, inst_create(.Unm, free_reg - 1, u16(free_reg - 1), 0), line)
    
    // Reg[2] = Reg[0] / Reg[1]; Top = Reg[3]
    chunk_append(chunk, inst_create(.Div, free_reg, u16(free_reg - 2), u16(free_reg - 1)), line)
    free_reg += 1
    
    top_reg := cast(u16)(free_reg - base_reg)
    
    // Return Reg[2] ..< Reg[3]
    chunk_append(chunk, inst_create(.Return, u8(top_reg - 1), top_reg - 1, 0), line)
    debug_disasm_chunk(chunk^)
    vm_interpret(vm, chunk)
}
