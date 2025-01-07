#+private
package lulu

import "core:log"
import "core:mem"

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
    reg := emit_expr_to_any_reg(compiler, expr)
    compiler_emit_return(compiler, reg, 1)
    compiler_end(compiler)
    vm.top = mem.ptr_offset(vm.base, compiler.free_reg)
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
@(private="file")
emit_expr_to_any_reg :: proc(compiler: ^Compiler, expr: ^Expr) -> (reg: u16) {
    // @todo 2025-01-06: Call `lcode.c:luaK_dischargevars(FuncState *fs, expdesc *e, int reg)` here

    // There should be a check above VNONRELOC.
    emit_expr_to_next_reg(compiler, expr)
    return expr.info.reg
}

// Analogous to `lcode.c:luaK_exp2nextreg(FuncState *fs, expdesc *e)` in Lua 5.1.
// See: https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2nextreg
@(private="file")
emit_expr_to_next_reg :: proc(compiler: ^Compiler, expr: ^Expr) {
    reserve_reg(compiler, 1)
    emit_expr_to_reg(compiler, expr, compiler.free_reg - 1)
}

// Analogous to `lcode.c:exp2reg(FuncState *fs, expdesc *e, int reg)` in Lua 5.1.
// Note that `discharge_expr_to_register()` will reassign fields in 'expr'.
@(private="file")
emit_expr_to_reg :: proc(compiler: ^Compiler, expr: ^Expr, reg: u16) {
    discharge_expr_to_reg(compiler, expr, reg)
    // @todo 2025-01-06: Implement the rest as needed...
}

// Analogous to `lcode.c:discharge2reg(FuncState *fs, expdesc *e, int reg)`
// See: https://www.lua.org/source/5.1/lcode.c.html#discharge2reg
@(private="file")
discharge_expr_to_reg :: proc(compiler: ^Compiler, expr: ^Expr, reg: u16) {
    // @todo 2025-01-06: Call `lcode.c:luaK_dischargevars(FuncState *fs, expdesc *e, int reg)` here
    #partial switch expr.type {
    case .Nil:      emit_nil(compiler, reg, 1)
    case .True:     emit_instruction(compiler, inst_create(.Boolean, reg, 1, 0))
    case .False:    emit_instruction(compiler, inst_create(.Boolean, reg, 0, 0))
    case .Number:
        index := compiler_add_constant(compiler, value_make_number(expr.info.number))
        emit_instruction(compiler, inst_create_ABx(.Constant, reg, index))
    case .Discharged:
        // Nothing to do
        return
    case:
        log.panicf("Cannot discharge '%v' to register", expr.type)
    }
    expr.type     = .Discharged
    expr.info.reg = reg
}

// Analogous to `lcode.c:luaK_reserveregs(FuncState *fs, int reg)` in Lua 5.1.
// See: https://www.lua.org/source/5.1/lcode.c.html#luaK_reserveregs
@(private="file")
reserve_reg :: proc(compiler: ^Compiler, #any_int count: int) {
    defer log.debug("free_reg :=", compiler.free_reg)
    // @todo 2025-01-06: Check the VM's available stack size?
    compiler.free_reg += cast(u16)count
}

@(private="file")
free_reg :: proc(compiler: ^Compiler, #any_int count: int) {
    defer log.debug("free_reg :=", compiler.free_reg)
    compiler.free_reg -= cast(u16)count
}

@(private="file")
emit_nil :: proc(compiler: ^Compiler, reg, count: u16) {
    chunk := current_chunk(compiler)
    pc    := len(chunk.code)
    folding: if pc > 0 {
        prev := &chunk.code[pc - 1]
        if prev.op != .Nil {
            break folding
        }
        // Ensure 'reg' is within range of 'prev' desired registers.
        if !(prev.a <= reg && reg <= prev.a + prev.b + 1) {
            break folding
        }
        log.debugf("Folding 'nil': %v => %v", prev.b, prev.b + count)
        prev.b += count
        return
    }
    // No optimization.
    emit_instruction(compiler, inst_create_AB(.Nil, reg, count))
}

// Analogous to `compiler.c:emitReturn()` in the book.
// Similar to Lua, all functions have this return even if they have explicit returns.
compiler_emit_return :: proc(compiler: ^Compiler, reg, n_results: u16) {
    // Add 1 because we want to differentiate from arg B == 0 indicating to return
    // up to top (a.k.a varargs).
    emit_instruction(compiler, inst_create_AB(.Return, reg, n_results + 1))
}

compiler_emit_ABx :: proc(compiler: ^Compiler, op: OpCode, reg: u16, index: u32) {
    assert(opcode_info[op].type == .Unsigned_Bx || opcode_info[op].type == .Signed_Bx)
    assert(opcode_info[op].c == .Unused)
    emit_instruction(compiler, inst_create_ABx(op, reg, index))
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

/* 
@brief Analogous to:
-   'lcode.c:luaK_code' in Lua 5.1.
-   'compiler.c:emitByte()' + 'compiler.c:emitBytes()' in the book.

@todo 2025-01-07:
-   Fix the line counter for folded constant expressions?
 */
@(private="file")
emit_instruction :: proc(compiler: ^Compiler, inst: Instruction) {
    chunk_append(current_chunk(compiler), inst, compiler.parser.consumed.line)
}

/* 
@see
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_prefix
-   https://the-ravi-programming-language.readthedocs.io/en/latest/lua-parser.html#state-transitions

@todo 2025-01-07:
-   fix register allocation for nonconstants! e.g: `true + -false`
-   make analog to `VRELOCABLE` perhaps (https://www.lua.org/source/5.1/lparser.h.html#VNONRELOC)
 */
compiler_emit_prefix :: proc(compiler: ^Compiler, op: OpCode, expr: ^Expr) {
    tmp := &Expr{}
    expr_set_number(tmp, 0)
    #partial switch op {
    case .Unm:
        // If non-constant, emit the operand before OpCode.Unm.
        if !expr_is_number(expr) {
            emit_expr_to_any_reg(compiler, expr)
        }
        emit_arith(compiler, .Unm, expr, tmp)
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
        emit_expr_to_reg_or_const(compiler, expr)
    }
}

// Not really postfix expressions, but more like fixing infix expressions past the fact?
// See: https://www.lua.org/source/5.1/lcode.c.html#luaK_posfix
compiler_fix_infix :: proc(compiler: ^Compiler, op: OpCode, left, right: ^Expr) {
    #partial switch op {
    case .Add ..= .Unm:
        emit_arith(compiler, op, left, right)
    case:
        log.panicf("Cannot emit posfix for '%v'", op)
    }
}

// https://www.lua.org/source/5.1/lcode.c.html#codearith
@(private="file")
emit_arith :: proc(compiler: ^Compiler, op: OpCode, left, right: ^Expr) {
    if folded_constants(op, left, right) {
        return
    }
    defer free_reg(compiler, 1)

    // 'right' is unused in this case
    if op == .Unm {
        reg := emit_expr_to_reg_or_const(compiler, left)
        emit_instruction(compiler, inst_create(op, reg, reg, 0))
        return
    }
    reg_a := emit_expr_to_reg_or_const(compiler, left)
    reg_b := emit_expr_to_reg_or_const(compiler, right)
    emit_instruction(compiler, inst_create(op, reg_a, reg_a, reg_b))
}

// Analogous to 'lcode.c:luaK_exp2RK(FuncState *fs, expdesc *e)'
// See: https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2RK
@(private="file")
emit_expr_to_reg_or_const :: proc(compiler: ^Compiler, expr: ^Expr) -> (reg: u16) {
    // @todo 2025-01-07: call analog to 'lcode.c:luaK_exp2val(FuncState *fs, expdesc *e)'
    // #partial switch type := expr.type; type {
    // case .Nil:      fallthrough
    // case .True:     fallthrough
    // case .False:    fallthrough
    // case .Number:
    //     // Constant can fit in RK operand?
    //     if len(current_chunk(compiler).code) <= MAX_RK {
    //         #partial switch type {
    //         case .Nil:
    //         case .Number:
    //         case .True:
    //         case .False:
    //         case:
    //         }
    //     }
    // case: break
    // }
    return emit_expr_to_any_reg(compiler, expr)
}

// See: https://www.lua.org/source/5.1/lcode.c.html#constfolding
@(private="file")
folded_constants :: proc(op: OpCode, left, right: ^Expr) -> (ok: bool) {
    // Can't fold two non-number-literals!
    if !expr_is_number(left) || !expr_is_number(right) {
        log.debug("Cannot do constant-folding")
        return false
    }
    x, y, result: f64 = left.info.number, right.info.number, 0
    #partial switch op {
    case .Add:  result = number_add(x, y)
    case .Sub:  result = number_sub(x, y)
    case .Mul:  result = number_mul(x, y)
    case .Div:
        // Avoid division by zero on the C side of things
        if x == 0 { return false }
        result = number_div(x, y)
    case .Mod:
        // Ditto
        if x == 0 { return false }
        result = number_mod(x, y)
    case .Pow:  result = number_pow(x, y)
    case .Unm:  result = number_unm(x)
    case:       log.panicf("Cannot fold opcode: %v", op)
    }
    if number_is_nan(result) {
        return false
    }
    expr_set_number(left, result)
    return true
}

@(private="file")
current_chunk :: proc(compiler: ^Compiler) -> (chunk: ^Chunk) {
    return compiler.chunk
}
