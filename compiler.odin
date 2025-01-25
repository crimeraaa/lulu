#+private
package lulu

import "core:fmt"
import "core:log"

_ :: fmt // needed for when !ODIN_DEBUG

MAX_CONSTANTS :: MAX_uBC

Compiler :: struct {
    vm:         ^VM,
    parent:     ^Compiler, // Enclosing state.
    parser:     ^Parser,   // All nested compilers share the same parser.
    chunk:      ^Chunk,    // Compilers do not own their chunks. They merely fill them.
    free_reg:    u16,      // Index of the first free register.
    stack_usage: int,
}

compiler_compile :: proc(vm: ^VM, chunk: ^Chunk, input: string) {
    parser   := &Parser{vm = vm, lexer = lexer_create(vm, input, chunk.source)}
    compiler := &Compiler{vm = vm, parser = parser, chunk = chunk}
    parser_advance(parser)
    for !parser_match(parser, .Eof) {
        parser_parse(parser, compiler)
    }
    parser_consume(parser, .Eof)
    compiler_end(compiler)
    vm.top = compiler.stack_usage
}

compiler_end :: proc(compiler: ^Compiler) {
    compiler_emit_return(compiler, 0, 0)
    when DEBUG_PRINT_CODE {
        fmt.printfln("=== STACK USAGE ===\n%i", compiler.stack_usage)
        debug_dump_chunk(compiler.chunk^)
    }
}

///=== REGISTER EMISSSION ======================================================


/*
Analogous to:
-   `lcode.c:luaK_reserveregs(FuncState *fs, int reg)` in Lua 5.1.

Links:
-    https://www.lua.org/source/5.1/lcode.c.html#luaK_reserveregs
 */
compiler_reserve_reg :: proc(compiler: ^Compiler, #any_int count: int) {
    // log.debugf("free_reg := %i + %i", compiler.free_reg, count, location = location)
    // @todo 2025-01-06: Check the VM's available stack size?
    compiler.free_reg += cast(u16)count
    if cast(int)compiler.free_reg > compiler.stack_usage {
        compiler.stack_usage = cast(int)compiler.free_reg
    }
}

compiler_pop_reg :: proc(compiler: ^Compiler, reg: u16, location := #caller_location) {
    // log.debugf("free_reg := %i - 1; reg := %i", compiler.free_reg, reg, location = location)
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
-   `expr` ALWAYS transition to `.Discharged` if it is not already so.
-   Use the `.reg` field to access which register it is currently in.

Returns
-   The register we stored `expr` in.
 */
compiler_expr_any_reg :: proc(compiler: ^Compiler, expr: ^Expr) -> (reg: u16) {
    compiler_discharge_vars(compiler, expr)

    // If already in a register don't waste time trying to re-emit it.
    // Doing so will also mess up any calls to 'compiler_pop_reg()'.
    if expr.type == .Discharged {
        // TODO(2025-01-08): Check if has jumps then check if non-local.
        return expr.reg
    }
    compiler_expr_next_reg(compiler, expr)
    return expr.reg
}

/*
Brief:
-   Analogous to `lcode.c:luaK_exp2nextreg(FuncState *fs, expdesc *e)` in Lua 5.1.

Note:
-   `expr` will most likely be transformed into `.Discharged`.

Links:
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2nextreg
 */
compiler_expr_next_reg :: proc(compiler: ^Compiler, expr: ^Expr) {
    compiler_reserve_reg(compiler, 1)
    compiler_expr_to_reg(compiler, expr, compiler.free_reg - 1)
}

/*
Brief:
-   Analogous to `lcode.c:exp2reg(FuncState *fs, expdesc *e, int reg)` in Lua 5.1.

Note:
-   `expr` will be transformed into `.Discharged`.
 */
compiler_expr_to_reg :: proc(compiler: ^Compiler, expr: ^Expr, reg: u16) {
    compiler_discharge_expr_to_reg(compiler, expr, reg)
    // @todo 2025-01-06: Implement the rest as needed...
}

/*
Notes:
-   Transforms `expr` into `.Discharged`, if it is not one already.
 */
compiler_discharge_expr_any_reg :: proc(compiler: ^Compiler, expr: ^Expr) {
    if expr.type != .Discharged {
        compiler_reserve_reg(compiler, 1)
        compiler_discharge_expr_to_reg(compiler, expr, compiler.free_reg - 1)
    }
}

/*
Brief:
-   Transforms `expr` to `.Discharged`.
-   Use `expr.reg` to get the register it resides in.

Analogous to:
-    `lcode.c:discharge2reg(FuncState *fs, expdesc *e, int reg)`

Links:
-   https://www.lua.org/source/5.1/lcode.c.html#discharge2reg
 */
compiler_discharge_expr_to_reg :: proc(compiler: ^Compiler, expr: ^Expr, reg: u16) {
    compiler_discharge_vars(compiler, expr)
    #partial switch expr.type {
    case .Nil:      compiler_emit_nil(compiler, reg, 1)
    case .True:     compiler_emit_ABC(compiler, .Load_Boolean, reg, 1, 0)
    case .False:    compiler_emit_ABC(compiler, .Load_Boolean, reg, 0, 0)
    case .Number:
        index := compiler_add_constant(compiler, value_make_number(expr.number))
        compiler_emit_ABx(compiler, .Load_Constant, reg, index)
    case .Constant:
        compiler_emit_ABx(compiler, .Load_Constant, reg, expr.index)
    case .Need_Register:
        compiler.chunk.code[expr.pc].a = reg
    case .Discharged:
        // TODO(2025-01-10): Nothing to do here until OP_MOVE analog is implemented
        return
    case:
        assert(expr.type == .Empty)
    }
    expr_init(expr, .Discharged)
    expr.reg = reg
}

/*
Analogous to:
-   `lcode.c:luaK_dischargevars(FuncState *fs, expdesc *e)`

Notes:
-   May transform `expr` into `.Need_Register`.
 */
compiler_discharge_vars :: proc(compiler: ^Compiler, expr: ^Expr) {
    #partial switch type := expr.type; type {
    case .Global:
        expr_init(expr, .Need_Register)
        expr.pc = compiler_emit_ABx(compiler, .Get_Global, 0, expr.index)
    }
}

/*
Analogous to:
-   'lcode.c:luaK_exp2RK(FuncState *fs, expdesc *e)' in Lua 5.1.

Links:
-    https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2RK
 */
compiler_expr_regconst :: proc(compiler: ^Compiler, expr: ^Expr) -> (reg: u16) {
    /*
    TODO(2025-01-07):
        call analog to 'lcode.c:luaK_exp2val(FuncState *fs, expdesc *e)'

    NOTE(2025-01-25):
        inline implementation of only relevant line of above.
     */
    compiler_discharge_vars(compiler, expr)

    #partial switch type := expr.type; type {
    case .Nil, .True, .False, .Number:
        chunk := compiler.chunk
        index: u32
        // Constant can fit in RK operand?
        if len(chunk.constants) <= MAX_INDEX_RK {
            #partial switch type {
            case .Nil:      index = chunk_add_constant(chunk, value_make_nil())
            case .True:     index = chunk_add_constant(chunk, value_make_boolean(true))
            case .False:    index = chunk_add_constant(chunk, value_make_boolean(false))
            case .Number:   index = chunk_add_constant(chunk, value_make_number(expr.number))
            case:           unreachable() // How?
            }
            expr_init(expr, .Constant)
            expr.index = index
            return rk_as_k(cast(u16)index)
        }
    case .Constant:
        // Constant can fit in argument C?
        if index := expr.index; index <= MAX_INDEX_RK {
            return rk_as_k(cast(u16)index)
        }
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
        next := reg + count - 1
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

compiler_emit_ABC :: proc(compiler: ^Compiler, op: OpCode, a, b, c: u16) -> (pc: int) {
    assert(opcode_info[op].type == .Separate)
    return compiler_emit_instruction(compiler, inst_create(op, a, b, c))
}

compiler_emit_AB :: proc(compiler: ^Compiler, op: OpCode, a, b: u16) -> (pc: int) {
    assert(opcode_info[op].type == .Separate)
    assert(opcode_info[op].a)
    assert(opcode_info[op].b != .Unused)
    return compiler_emit_instruction(compiler, inst_create(op, a, b, 0))
}

compiler_emit_ABx :: proc(compiler: ^Compiler, op: OpCode, reg: u16, index: u32) -> (pc: int) {
    assert(opcode_info[op].type == .Unsigned_Bx || opcode_info[op].type == .Signed_Bx)
    assert(opcode_info[op].c == .Unused)
    return compiler_emit_instruction(compiler, inst_create_ABx(op, reg, index))
}

/*
Analogous to:
-   'lcode.c:luaK_code' in Lua 5.1.
-   'compiler.c:emitByte()' + 'compiler.c:emitBytes()' in the book.

TODO(2025-01-07):
-   Fix the line counter for folded constant expressions?
 */
compiler_emit_instruction :: proc(compiler: ^Compiler, inst: Instruction) -> (pc: int) {
    vm    := compiler.vm
    chunk := compiler.chunk
    return chunk_append(vm, chunk, inst, compiler.parser.consumed.line)
}

///=============================================================================


// Analogous to 'compiler.c:makeConstant()' in the book.
compiler_add_constant :: proc(compiler: ^Compiler, constant: Value) -> (index: u32) {
    index = chunk_add_constant(compiler.chunk, constant)
    if index >= MAX_CONSTANTS {
        parser_error_consumed(compiler.parser, "Function uses too many constants")
    }
    return index
}

/*
Notes:
-   Assumes the target operand was just consumed and now resides in `Expr`.
 */
compiler_emit_not :: proc(compiler: ^Compiler, expr: ^Expr) {
    compiler_discharge_vars(compiler, expr)

    /*
    Details:
    -   In the call to `parser.odin:unary()`, we should have consumed the
        target operand beforehand.
    -   Thus, it should be able to be discharged (if it is not already!)
    -   This means we should be able to pop it as well.
     */
    when USE_CONSTANT_FOLDING {
        #partial switch expr.type {
        case .Nil, .False:
            expr.type = .True
            return
        case .True, .Number, .Constant:
            expr.type = .False
            return
        case .Discharged, .Need_Register:
            break
        case: unreachable()
        }
    }
    compiler_discharge_expr_any_reg(compiler, expr)
    compiler_pop_expr(compiler, expr)

    expr_init(expr, .Need_Register)
    expr.pc = compiler_emit_AB(compiler, .Not, 0, expr.reg)
}

/*
Notes:
-   when `!USE_CONSTANT_FOLDING`, we expect that if `left` was a literal then
    we called `compiler_expr_regconst()` beforehand.
-   Otherwise, the order of arguments will be reversed!

Links:
-   https://www.lua.org/source/5.1/lcode.c.html#codearith
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_posfix
 */
compiler_emit_arith :: proc(compiler: ^Compiler, op: OpCode, left, right: ^Expr) {
    assert(.Add <= op && op <= .Unm || op == .Concat)
    when USE_CONSTANT_FOLDING {
        if compiler_fold_constant_arith(op, left, right) {
            return
        }
    }

    // When OpCode.Unm, right is unused.
    rk_c, rk_b: u16
    if op == .Unm {
        rk_b = compiler_expr_any_reg(compiler, left)
    } else {
        rk_c = compiler_expr_regconst(compiler, right)
        rk_b = compiler_expr_regconst(compiler, left)
    }

    // In the event BOTH are .Discharged, we want to pop them in the correct
    // order! Otherwise the assert in `compiler_pop_reg()` will fail.
    if rk_b > rk_c {
        compiler_pop_expr(compiler, left)
        compiler_pop_expr(compiler, right)
    } else {
        compiler_pop_expr(compiler, right)
        compiler_pop_expr(compiler, left)
    }

    // Argument A will be fixed down the line.
    expr_init(left, .Need_Register)
    left.pc = compiler_emit_ABC(compiler, op, 0, rk_b, rk_c)
}

compiler_emit_compare :: proc(compiler: ^Compiler, op: OpCode, left, right: ^Expr) {
    assert(.Eq <= op && op <= .Geq)

    rk_b := compiler_expr_regconst(compiler, left)
    rk_c := compiler_expr_regconst(compiler, right)

    if rk_b > rk_c {
        compiler_pop_expr(compiler, left)
        compiler_pop_expr(compiler, right)
    } else {
        compiler_pop_expr(compiler, right)
        compiler_pop_expr(compiler, left)
    }

    expr_init(left, .Need_Register)
    left.pc = compiler_emit_ABC(compiler, op, 0, rk_b, rk_c)
}

/*
Links:
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_posfix
 */
compiler_emit_concat :: proc(compiler: ^Compiler, left, right: ^Expr) {
    // TODO(2025-01-12): call analog to 'lcode.c:luaK_exp2val(FuncState *fs, expdesc *e)' here
    compiler_discharge_vars(compiler, right)

    chunk := compiler.chunk
    code  := chunk.code[:]

    // This is past the first consecutive concat, so we can fold them.
    if right.type == .Need_Register && code[right.pc].op == .Concat {
        instr := &code[right.pc]
        assert(left.reg == instr.b - 1)
        compiler_pop_expr(compiler, left)
        instr.b = left.reg
        expr_init(left, .Need_Register)
        left.pc = right.pc
        return
    }
    // This is the first in a potential chain of concats.
    compiler_expr_next_reg(compiler, right)
    compiler_emit_arith(compiler, .Concat, left, right)
}

/*
Notes:
-   If `expr` is `.Discharged`, it MUST be the most recently discharged register
    in order to be able to pop it.
 */
compiler_pop_expr :: proc(compiler: ^Compiler, expr: ^Expr) {
    // if e->k == VNONRELOC
    if expr.type == .Discharged {
        compiler_pop_reg(compiler, expr.reg)
    }
}

// See: https://www.lua.org/source/5.1/lcode.c.html#constfolding
compiler_fold_constant_arith :: proc(op: OpCode, left, right: ^Expr) -> (ok: bool) {
    // Can't fold two non-number-literals!
    if !expr_is_number(left) || !expr_is_number(right) {
        return false
    }
    x, y, result: f64 = left.number, right.number, 0
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
    case: unreachable()
    }
    if number_is_nan(result) {
        return false
    }
    expr_set_number(left, result)
    return true
}
