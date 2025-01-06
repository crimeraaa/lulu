#+private
package lulu

import "core:log"

MAX_CONSTANTS :: MAX_uBC

Compiler :: struct {
    parent: ^Compiler,  // Enclosing state.
    parser: ^Parser,    // All nested compilers share the same parser.
    chunk:  ^Chunk,     // Compilers do not own their chunks. They merely fill them.
    free_reg: u16,      // Index of the first free register.
}

compiler_compile :: proc(vm: ^VM, chunk: ^Chunk, input: string) -> (ok: bool) {
    parser   := &Parser{lexer = lexer_create(input, chunk.source)}
    compiler := &Compiler{parser = parser, chunk = chunk}
    expr     := &Expr{}
    // defer log.debug("expr: ", expr)
    parser_advance(parser) or_return
    parser_parse_expression(parser, compiler, expr) or_return
    parser_consume(parser, .Eof) or_return

    // For now we assume 'expression()' results in exactly one constant value
    reg := compiler_emit_expr_to_any_register(compiler, expr)
    compiler_emit_return(compiler, cast(u16)reg, 1)
    compiler_end(compiler)
    vm.top = ptr_add(vm.base, compiler.free_reg)
    return !parser.panicking
}

compiler_end :: proc(compiler: ^Compiler) {
    compiler_emit_return(compiler, 0, 0)
    when DEBUG_PRINT_CODE {
        if !compiler.parser.panicking {
            debug_disasm_chunk(current_chunk(compiler)^)
        }
    }
}

// Analogous to `lcode.c:luaK_exp2anyreg(FuncState *fs, expdesc *e)` in Lua 5.1.
// See: https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2anyreg
// Returns a register.
compiler_emit_expr_to_any_register :: proc(compiler: ^Compiler, expr: ^Expr) -> int {
    // @todo 2025-01-06: Call `lcode.c:luaK_dischargevars(FuncState *fs, expdesc *e, int reg)` here

    // There should be a check above VNONRELOC.
    compiler_emit_expr_to_next_register(compiler, expr)
    return expr.info
}

// Analogous to `lcode.c:luaK_exp2nextreg(FuncState *fs, expdesc *e)` in Lua 5.1.
// See: https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2nextreg
compiler_emit_expr_to_next_register :: proc(compiler: ^Compiler, expr: ^Expr) {
    compiler_reserve_registers(compiler, 1)
    expr_to_register(compiler, expr, compiler.free_reg - 1)
}

// Analogous to `lcode.c:luaK_reserveregs(FuncState *fs, int reg)` in Lua 5.1.
// See: https://www.lua.org/source/5.1/lcode.c.html#luaK_reserveregs
compiler_reserve_registers :: proc(compiler: ^Compiler, #any_int count: int) {
    // @todo 2025-01-06: Check the VM's available stack size?
    compiler.free_reg += cast(u16)count
}

// Analogous to `lcode.c:exp2reg(FuncState *fs, expdesc *e, int reg)` in Lua 5.1.
// Note that `discharge_expr_to_register()` will reassign fields in 'expr'.
@(private="file")
expr_to_register :: proc(compiler: ^Compiler, expr: ^Expr, reg: u16) {
    discharge_expr_to_register(compiler, expr, reg)
    // @todo 2025-01-06: Implement the rest as needed...
}

// Analogous to `lcode.c:discharge2reg(FuncState *fs, expdesc *e, int reg)`
// See: https://www.lua.org/source/5.1/lcode.c.html#discharge2reg
@(private="file")
discharge_expr_to_register :: proc(compiler: ^Compiler, expr: ^Expr, reg: u16) {
    // @todo 2025-01-06: Call `lcode.c:luaK_dischargevars(FuncState *fs, expdesc *e, int reg)` here
    #partial switch expr.type {
    case .Number:
        index := compiler_add_constant(compiler, expr.value)
        inst := inst_create_ABx(.Constant, reg, index)
        compiler_emit_instruction(compiler, inst)
    case:
        log.panicf("Cannot discharge '%v' to register", expr.type)
    }
    expr.type = .Discharged
    expr.info = cast(int)reg
}

// Analogous to `compiler.c:emitReturn()` in the book.
// Similar to Lua, all functions have this return even if they have explicit returns.
compiler_emit_return :: proc(compiler: ^Compiler, reg, n_results: u16) {
    // Add 1 because we want to differentiate from arg B == 0 indicating to return
    // up to top.
    inst := inst_create_AB(.Return, reg, n_results + 1)
    compiler_emit_instruction(compiler, inst)
}

compiler_emit_ABx :: proc(compiler: ^Compiler, op: OpCode, reg: u16, index: u32) {
    assert(opcode_info[op].type == .Unsigned_Bx || opcode_info[op].type == .Signed_Bx)
    assert(opcode_info[op].c == .Unused)
    inst := inst_create_ABx(op, reg, index)
    compiler_emit_instruction(compiler, inst)
}

// Analogous to 'compiler.c:makeConstant()' in the book.
compiler_add_constant :: proc(compiler: ^Compiler, constant: Value) -> (index: u32) {
    index = chunk_add_constant(current_chunk(compiler), constant)
    if index >= MAX_CONSTANTS {
        parser_error_consumed(compiler.parser, "Function uses too many constants")
        return 0
    }
    return index
}

// Analogous to 'compiler.c:emitByte()' and 'compiler.c:emitBytes()' in the book.
// @todo 2025-01-07: Fix the line counter!
compiler_emit_instruction :: proc(compiler: ^Compiler, inst: Instruction) {
    chunk_append(current_chunk(compiler), inst, compiler.parser.consumed.line)
}

// See: https://www.lua.org/source/5.1/lcode.c.html#luaK_prefix
compiler_emit_prefix :: proc(compiler: ^Compiler, op: OpCode, expr: ^Expr) {
    tmp := &Expr{}
    expr_init(tmp, .Number, 0)
    tmp.value = 0
    #partial switch op {
    case .Unm:
        if expr_is_number(expr) {
            emit_arith(compiler, .Unm, expr, tmp)
        }
        // @todo 2025-01-06: Implement analog to 'lcode.c:luaK_expr2anyreg()'
    case:   log.panicf("Cannot emit '%v'", op)
    }
}

// It seems this emits intermediates for nonconstant infix expressions?
// See: https://www.lua.org/source/5.1/lcode.c.html#luaK_infix
compiler_emit_infix :: proc(compiler: ^Compiler, op: OpCode, expr: ^Expr) {
    #partial switch op {
    case .Add ..= .Pow:
        if !expr_is_number(expr) {
            // call luaK_exp2RK()
        }
    case:
        // @todo 2025-01-06: Emit analog to 'lcode.c:luaK_exp2RK'
        log.panicf("Cannot emit infix for '%v'", op)
    }
}

// Not really postfix expressions, but more like fixing infix expressions past the fact?
// See: https://www.lua.org/source/5.1/lcode.c.html#luaK_posfix
compiler_emit_postfix :: proc(compiler: ^Compiler, op: OpCode, a, b: ^Expr) {
    #partial switch op {
    case .Add ..= .Unm:
        emit_arith(compiler, op, a, b)
    case:
        log.panicf("Cannot emit posfix for '%v'", op)
    }
}

// https://www.lua.org/source/5.1/lcode.c.html#codearith
@(private="file")
emit_arith :: proc(compiler: ^Compiler, op: OpCode, a, b: ^Expr) {
    if folded_constants(op, a, b) {
        return
    }
    // @todo 2025-01-06: Emit registers! See 'lcode.c:codearith()'.
}

// See: https://www.lua.org/source/5.1/lcode.c.html#constfolding
@(private="file")
folded_constants :: proc(op: OpCode, a, b: ^Expr) -> (ok: bool) {
    // Can't fold two non-number-literals!
    if !expr_is_number(a) || !expr_is_number(b) {
        return false
    }
    x, y, result: Number = a.value, b.value, 0
    #partial switch op {
    case .Add:  result = x + y
    case .Sub:  result = x - y
    case .Mul:  result = x * y
    case .Div:
        // Avoid division by zero on the C side of things
        if x == 0 { return false }
        result = x / y
    case .Mod:
        // Ditto
        if x == 0 { return false }
        result = number_mod(x, y)
    case .Pow:  result = number_pow(x, y)
    case .Unm:  result = -x
    case:       log.panicf("Cannot fold opcode: %v", op)
    }
    if number_is_nan(result) {
        return false
    }
    a.value = result
    return true
}

@(private="file")
current_chunk :: proc(compiler: ^Compiler) -> (chunk: ^Chunk) {
    return compiler.chunk
}
