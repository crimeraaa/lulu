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
    stack_usage: int,
}

compiler_compile :: proc(vm: ^VM, chunk: ^Chunk, input: string) -> (ok: bool) {
    parser   := &Parser{lexer = lexer_create(input, chunk.source)}
    compiler := &Compiler{parser = parser, chunk = chunk}
    expr     := &Expr{}

    parser_advance(parser) or_return
    parser_parse_expression(parser, compiler, expr) or_return
    parser_consume(parser, .Eof) or_return

    // For now we assume 'expression()' results in exactly one constant value
    reg := compiler_expr_any_reg(compiler, expr)
    compiler_emit_return(compiler, reg, 1)
    compiler_end(compiler)
    vm.top = &vm.base[compiler.stack_usage]
    return !parser.panicking
}

import "core:fmt"

compiler_end :: proc(compiler: ^Compiler) {
    compiler_emit_return(compiler, 0, 0)
    when DEBUG_PRINT_CODE {
        fmt.println("=== STACK USAGE ===\n", compiler.stack_usage)
        if !compiler.parser.panicking {
            debug_dump_chunk(compiler.chunk^)
        }
    }
}

///=== REGISTER EMISSSION ======================================================


/*
Analogous to:
-   `lcode.c:luaK_reserveregs(FuncState *fs, int reg)` in Lua 5.1.

Links:
-    https://www.lua.org/source/5.1/lcode.c.html#luaK_reserveregs
 */
compiler_reserve_reg :: proc(compiler: ^Compiler, #any_int count: int, location := #caller_location) {
    log.debugf("free_reg := %i + %i", compiler.free_reg, count, location = location)
    // @todo 2025-01-06: Check the VM's available stack size?
    compiler.free_reg += cast(u16)count
    if cast(int)compiler.free_reg > compiler.stack_usage {
        compiler.stack_usage = cast(int)compiler.free_reg
    }
}

compiler_free_reg :: proc(compiler: ^Compiler, reg: u16, location := #caller_location) {
    log.debugf("free_reg := %i - 1; reg := %i", compiler.free_reg, reg, location = location)
    if !rk_is_k(reg) /* && reg >= fs->nactvar */ {
        compiler.free_reg -= 1
        log.assertf(reg == compiler.free_reg, "free_reg := %i but reg := %i",
            compiler.free_reg, reg, loc = location)
    }
}

/*
Brief
-   Analogous to `lcode.c:luaK_exp2anyreg(FuncState *fs, expdesc *e)` in Lua 5.1.

Links
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2anyreg

Note:
-   `expr` will be mutated if it is not already discharged.

Returns
-   A register.
 */
compiler_expr_any_reg :: proc(compiler: ^Compiler, expr: ^Expr) -> (reg: u16) {
    // @todo 2025-01-06: Call `lcode.c:luaK_dischargevars(FuncState *fs, expdesc *e, int reg)` here

    // If already in a register don't waste time trying to re-emit it.
    // Doing so will also mess up any calls to 'reg_free()'.
    if expr.type == .Discharged {
        // TODO(2025-01-08): Check if has jumps then check if non-local.
        return expr.info.reg
    }
    compiler_expr_next_reg(compiler, expr)
    return expr.info.reg
}

/*
Brief:
-   Analogous to `lcode.c:luaK_exp2nextreg(FuncState *fs, expdesc *e)` in Lua 5.1.

Note:
-   `expr` will most likely be mutated.

Links:
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2nextreg
 */
@(private="file")
compiler_expr_next_reg :: proc(compiler: ^Compiler, expr: ^Expr) {
    compiler_reserve_reg(compiler, 1)
    compiler_expr_to_reg(compiler, expr, compiler.free_reg - 1)
}

/*
Brief:
-   Analogous to `lcode.c:exp2reg(FuncState *fs, expdesc *e, int reg)` in Lua 5.1.

Note:
-   `discharge_expr_to_register()` will mutate `expr`.
 */
compiler_expr_to_reg :: proc(compiler: ^Compiler, expr: ^Expr, reg: u16) {
    compiler_discharge_expr_to_reg(compiler, expr, reg)
    // @todo 2025-01-06: Implement the rest as needed...
}

// Analogous to `lcode.c:discharge2reg(FuncState *fs, expdesc *e, int reg)`
// See: https://www.lua.org/source/5.1/lcode.c.html#discharge2reg
compiler_discharge_expr_to_reg :: proc(compiler: ^Compiler, expr: ^Expr, reg: u16) {
    // @todo 2025-01-06: Call `lcode.c:luaK_dischargevars(FuncState *fs, expdesc *e, int reg)` here
    #partial switch expr.type {
    case .Nil:      compiler_emit_nil(compiler, reg, 1)
    case .True:     compiler_emit_ABC(compiler, .Load_Boolean, reg, 1, 0)
    case .False:    compiler_emit_ABC(compiler, .Load_Boolean, reg, 0, 0)
    case .Number:
        index := compiler_add_constant(compiler, value_make_number(expr.info.number))
        compiler_emit_ABx(compiler, .Load_Constant, reg, index)
    case .Constant:
        compiler_emit_ABx(compiler, .Load_Constant, reg, expr.info.index)
    case .Need_Register:
        compiler.chunk.code[expr.info.pc].a = reg
    case .Discharged:
        // Nothing to do
        return
    case:
        unreachable()
    }
    expr.type     = .Discharged
    expr.info.reg = reg
}

/*
Analogous to:
-   'lcode.c:luaK_exp2RK(FuncState *fs, expdesc *e)' in Lua 5.1.

Links:
-    https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2RK
 */
compiler_expr_to_regconst :: proc(compiler: ^Compiler, expr: ^Expr) -> (reg: u16) {
    // @todo 2025-01-07: call analog to 'lcode.c:luaK_exp2val(FuncState *fs, expdesc *e)'
    #partial switch type := expr.type; type {
    case .Nil:      fallthrough
    case .True:     fallthrough
    case .False:    fallthrough
    case .Number:
        chunk := compiler.chunk
        index: u32
        // Constant can fit in RK operand?
        if len(chunk.constants) <= MAX_INDEX_RK {
            #partial switch type {
            case .Nil:      index = chunk_add_constant(chunk, value_make_nil())
            case .True:     index = chunk_add_constant(chunk, value_make_boolean(true))
            case .False:    index = chunk_add_constant(chunk, value_make_boolean(false))
            case .Number:   index = chunk_add_constant(chunk, value_make_number(expr.info.number))
            case:           unreachable() // How?
            }
            expr.info.index = index
            expr.type       = .Constant
            return rk_as_k(cast(u16)index)
        }
    case .Constant:
        // Constant can fit in argument C?
        if index := expr.info.index; index <= MAX_INDEX_RK {
            return rk_as_k(cast(u16)index)
        }
    case: break
    }
    return compiler_expr_any_reg(compiler, expr)
}

///=============================================================================

///=== INSTRUCTION EMISSION ====================================================

compiler_emit_nil :: proc(compiler: ^Compiler, reg, count: u16) {
    assert(count != 0, "Emitting 0 nils is invalid!")

    chunk := compiler.chunk
    pc    := len(chunk.code)
    folding: if pc > 0 {
        prev := &chunk.code[pc - 1]
        if prev.op != .Load_Nil {
            break folding
        }
        // Ensure 'reg' is within range of 'prev' desired registers.
        if !(prev.a <= reg && reg <= prev.b + 1) {
            break folding
        }
        log.debugf("Folding 'nil': %v => %v", prev.b, prev.b + count)
        next := cast(u16)(reg + count - 1)
        if next <= prev.b {
            break folding
        }
        prev.b = next
        return
    }
    // No optimization.
    compiler_emit_AB(compiler, .Load_Nil, reg, reg + count - 1)
}

// Analogous to `compiler.c:emitReturn()` in the book.
// Similar to Lua, all functions have this return even if they have explicit returns.
compiler_emit_return :: proc(compiler: ^Compiler, reg, n_results: u16) {
    // Add 1 because we want to differentiate from arg B == 0 indicating to return
    // up to top (a.k.a varargs).
    compiler_emit_AB(compiler, .Return, reg, n_results + 1)
}

compiler_emit_ABC :: proc(compiler: ^Compiler, op: OpCode, a, b, c: u16) -> int {
    assert(opcode_info[op].type == .Separate)
    return compiler_emit_instruction(compiler, inst_create(op, a, b, c))
}

compiler_emit_AB :: proc(compiler: ^Compiler, op: OpCode, a, b: u16) {
    assert(opcode_info[op].type == .Separate)
    assert(opcode_info[op].a)
    assert(opcode_info[op].b != .Unused)
    compiler_emit_instruction(compiler, inst_create(op, a, b, 0))
}

compiler_emit_ABx :: proc(compiler: ^Compiler, op: OpCode, reg: u16, index: u32) {
    assert(opcode_info[op].type == .Unsigned_Bx || opcode_info[op].type == .Signed_Bx)
    assert(opcode_info[op].c == .Unused)
    compiler_emit_instruction(compiler, inst_create_ABx(op, reg, index))
}

/*
Analogous to:
-   'lcode.c:luaK_code' in Lua 5.1.
-   'compiler.c:emitByte()' + 'compiler.c:emitBytes()' in the book.

TODO(2025-01-07):
-   Fix the line counter for folded constant expressions?
 */
compiler_emit_instruction :: proc(compiler: ^Compiler, inst: Instruction) -> (pc: int) {
    return chunk_append(compiler.chunk, inst, compiler.parser.consumed.line)
}

///=============================================================================


// Analogous to 'compiler.c:makeConstant()' in the book.
compiler_add_constant :: proc(compiler: ^Compiler, constant: Value) -> (index: u32) {
    index = chunk_add_constant(compiler.chunk, constant)
    if index >= MAX_CONSTANTS {
        parser_error_consumed(compiler.parser, "Function uses too many constants")
        return 0
    }
    return index
}

// https://www.lua.org/source/5.1/lcode.c.html#codearith
compiler_emit_arith :: proc(compiler: ^Compiler, op: OpCode, left, right: ^Expr) {
    when CONSTANT_FOLDING_ENABLED {
        if compiler_fold_constants(op, left, right) {
            return
        }
    }

    // When OpCode.Unm, right is unused.
    rk_b := compiler_expr_to_regconst(compiler, right) if op != .Unm else 0
    rk_a := compiler_expr_to_regconst(compiler, left)

    // I'm unsure WHY this is needed.
    if rk_a > rk_b {
        compiler_free_expr(compiler, left)
        compiler_free_expr(compiler, right)
    } else {
        compiler_free_expr(compiler, right)
        compiler_free_expr(compiler, left)
    }

    // Argument A will be fixed down the line.
    left.info.pc = compiler_emit_ABC(compiler, op, 0, rk_a, rk_b)
    left.type    = .Need_Register
}

compiler_free_expr :: proc(compiler: ^Compiler, expr: ^Expr) {
    // if e->k == VNONRELOC
    if expr.type == .Discharged {
        compiler_free_reg(compiler, expr.info.reg)
    }
}

// See: https://www.lua.org/source/5.1/lcode.c.html#constfolding
compiler_fold_constants :: proc(op: OpCode, left, right: ^Expr) -> (ok: bool) {
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
    case:       unreachable()
    }
    if number_is_nan(result) {
        return false
    }
    expr_set_number(left, result)
    return true
}
