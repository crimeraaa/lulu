#+private
package lulu

import "core:fmt"
import "core:log"

_ :: fmt // needed for when !ODIN_DEBUG

MAX_CONSTANTS :: MAX_uBC

Compiler :: struct {
    vm:             ^VM,
    scope_depth:     int,      // How far down is our current lexical scope?
    parent:         ^Compiler, // Enclosing state.
    parser:         ^Parser,   // All nested compilers share the same parser.
    chunk:          ^Chunk,    // Compilers do not own their chunks. They merely fill them.
    free_reg:        int,      // Index of the first free register.
    last_jump:       int,
    is_print:        bool,     // HACK(2025-04-09): until `print` is a global runtime function
    active_local:    int,      // How many locals are currently in scope?
}

compiler_init :: proc(compiler: ^Compiler, vm: ^VM, parser: ^Parser, chunk: ^Chunk) {
    compiler.vm     = vm
    compiler.parser = parser
    compiler.chunk  = chunk
    compiler.last_jump = -1
}

compiler_compile :: proc(vm: ^VM, chunk: ^Chunk, input: string) {
    parser   := &Parser{vm = vm, lexer = lexer_create(vm, input, chunk.source)}
    compiler := &Compiler{}
    compiler_init(compiler, vm, parser, chunk)
    parser_advance(parser)

    // Top level scope can also have its own locals.
    compiler_begin_scope(compiler)
    for !parser_match(parser, .Eof) {
        parser_parse(parser, compiler)
    }
    parser_consume(parser, .Eof)
    compiler_end_scope(compiler)
    compiler_end(compiler)
}

compiler_end :: proc(compiler: ^Compiler) {
    compiler_code_return(compiler, 0, 0)
    when DEBUG_PRINT_CODE {
        debug_dump_chunk(compiler.chunk^)
    }
}

compiler_begin_scope :: proc(compiler: ^Compiler) {
    compiler.scope_depth += 1
}


/*
Analogous to:
-   `lparser.c:removevars(LexState *ls, int tolevel)` in Lua 5.1.5.
 */
compiler_end_scope :: proc(compiler: ^Compiler) {
    depth := compiler.scope_depth - 1
    count := compiler.active_local

    for count > 0 && compiler.chunk.locals[count - 1].depth > depth do count -= 1

    // WARNING(2025-04-09): Is this safe? We need these to reuse registers once
    // locals go out of scope.
    compiler.free_reg     = count
    compiler.active_local = count
    compiler.scope_depth  = depth
}


///=== REGISTER EMISSSION ======================================================


/*
Analogous to:
-   `lcode.c:luaK_reserveregs(FuncState *fs, int reg)` in Lua 5.1.5.

Links:
-    https://www.lua.org/source/5.1/lcode.c.html#luaK_reserveregs
 */
compiler_reserve_reg :: proc(compiler: ^Compiler, count: int) {
    // log.debugf("free_reg := %i + %i", compiler.free_reg, count, location = location)
    // @todo 2025-01-06: Check the VM's available stack size?
    compiler.free_reg += count
    if compiler.free_reg > compiler.chunk.stack_used {
        compiler.chunk.stack_used = compiler.free_reg
    }
}


compiler_pop_reg :: proc(compiler: ^Compiler, reg: u16, location := #caller_location) {
    // Only pop if nonconstant and not the register of an existing local.
    if !reg_is_k(reg) && cast(int)reg >= compiler.active_local {
        compiler.free_reg -= 1
        log.assertf(cast(int)reg == compiler.free_reg, "free_reg := %i but reg := %i",
            compiler.free_reg, reg, loc = location)
    }
}


/*
Assumptions:
-   If `expr` is `.Discharged`, and its register does not refer to an *existing*
    local, it MUST be the most recently discharged register in order to be able
    to pop it.
 */
compiler_expr_pop :: proc(compiler: ^Compiler, expr: Expr, location := #caller_location) {
    // if e->k == VNONRELOC
    if expr.type == .Discharged {
        compiler_pop_reg(compiler, expr.reg, location = location)
    }
}


/*
Analogous to:
-   `lcode.c:luaK_exp2anyreg(FuncState *fs, expdesc *e)` in Lua 5.1.5.

Guarantees:
-   `expr` ALWAYS transitions to `.Discharged` if it is not already so.
-   Even if `expr` is a literal or a constant, it will be pushed.
-   If `expr` is already `.Discharged`, we simply return its register.
-   Use `expr.reg` to access which register it is currently in.

Returns:
-   The register we stored `expr` in.

Links
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2anyreg
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
Overview:
-   Pushes `expr` to the top of the stack no matter what.

Analogous to:
-   `lcode.c:luaK_exp2nextreg(FuncState *fs, expdesc *e)` in Lua 5.1.5.

Guarantees:
-   `expr` will be transformed to type `.Discharged` if it is not so already.
-   Even if `expr` is `.Discharged`, it will be pushed regardless.

Links:
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2nextreg
 */
compiler_expr_next_reg :: proc(compiler: ^Compiler, expr: ^Expr, location := #caller_location) {
    compiler_discharge_vars(compiler, expr, location = location)
    compiler_expr_pop(compiler, expr^, location = location)

    compiler_reserve_reg(compiler, 1)
    compiler_expr_to_reg(compiler, expr, cast(u16)compiler.free_reg - 1)
}

/*
Overview
-   Assigns `expr` to register `reg`. `expr` will always be transformed into
    type `.Discharged`.
-   Typically only called by other functions which simply want to push to the
    index of the first free register `compiler.free_reg`.

Analogous to
-   `lcode.c:exp2reg(FuncState *fs, expdesc *e, int reg)` in Lua 5.1.5
 */
compiler_expr_to_reg :: proc(compiler: ^Compiler, expr: ^Expr, reg: u16) {
    compiler_discharge_to_reg(compiler, expr, reg)
    // @todo 2025-01-06: Implement the rest as needed...

    // NOTE(2025-04-09): Seemingly redundant but useful when we get to jumps.
    // expr.f = NO_JUMP; expr.t = NO_JUMP;
    expr_set_reg(expr, .Discharged, reg)
}


/*
Overview
-   Very similar to `compiler_discharge_to_reg()`
-   We do nothing if `expr` is already of type `.Discharged`.

Analogous to:
-    `lcode.c:exp2anyreg(FuncState *fs, expdesc *e)`.
 */
compiler_discharge_any_reg :: proc(compiler: ^Compiler, expr: ^Expr, location := #caller_location) {
    if expr.type != .Discharged {
        compiler_reserve_reg(compiler, 1)
        compiler_discharge_to_reg(compiler, expr, cast(u16)compiler.free_reg - 1, location = location)
    }
}


/*
Overview
-   Transforms `expr` to `.Discharged` no matter what.
-   This may involve emitting get-operations for literals, constants, globals,
    locals, or table indexes.

Analogous to:
-    `lcode.c:discharge2reg(FuncState *fs, expdesc *e, int reg)`

Guarantees:
-   `expr.reg` holds the register we just emitted `expr` to.

Links:
-   https://www.lua.org/source/5.1/lcode.c.html#discharge2reg
 */
compiler_discharge_to_reg :: proc(compiler: ^Compiler, expr: ^Expr, reg: u16, location := #caller_location) {
    compiler_discharge_vars(compiler, expr, location = location)
    #partial switch expr.type {
    case .Nil:      compiler_code_nil(compiler, reg, 1)
    case .True:     compiler_code_ABC(compiler, .Load_Boolean, reg, 1, 0)
    case .False:    compiler_code_ABC(compiler, .Load_Boolean, reg, 0, 0)
    case .Number:
        index := compiler_add_constant(compiler, value_make_number(expr.number))
        compiler_code_ABx(compiler, .Load_Constant, reg, index)
    case .Constant:
        compiler_code_ABx(compiler, .Load_Constant, reg, expr.index)
    case .Need_Register:
        compiler.chunk.code[expr.pc].a = reg
    case .Discharged:
        // e.g. getting a local variable
        if reg != expr.reg {
            compiler_code_ABC(compiler, .Move, reg, expr.reg, 0)
        }
    case:
        assert(expr.type == .Empty)
    }
    expr_set_reg(expr, .Discharged, reg)
}

/*
Overview
-   Readies the retrieval of expressions representing variables.
-   For other types of expressions nothing occurs.

Analogous to:
-   `lcode.c:luaK_dischargevars(FuncState *fs, expdesc *e)` in Lua 5.1.5.

Guarantees:
-   Bytecode is emitted for globals and tables, but destination registers are
    not yet set. `expr` is transformed to `.Need_Register`.
-   Locals are transformed to `.Discharged` because they already have a known
    register.
 */
compiler_discharge_vars :: proc(compiler: ^Compiler, expr: ^Expr, location := #caller_location) {
    #partial switch type := expr.type; type {
    case .Global:
        pc := compiler_code_ABx(compiler, .Get_Global, 0, expr.index)
        expr_set_pc(expr, .Need_Register, pc)
    case .Local:
        // info is already the local register we resolved beforehand.
        expr_init(expr, .Discharged)
    case .Table_Index:
        // We can now reuse the registers allocated for the table and index.
        compiler_pop_reg(compiler, expr.table.index, location = location)
        compiler_pop_reg(compiler, expr.table.reg, location = location)
        pc := compiler_code_ABC(compiler, .Get_Table, 0, expr.table.reg, expr.table.index)
        expr_set_pc(expr, .Need_Register, pc)
    }
}


/*
Analogous to:
-   `lcode.c:luaK_exp2val(FuncState *fs, expdesc *e)` in Lua 5.1.5.
 */
compiler_expr_to_value :: proc(compiler: ^Compiler, expr: ^Expr) {
    // if hasjumps(expr) luaK_exp2anyreg(fs, e) else
    compiler_discharge_vars(compiler, expr)
}


/*
Overview
-   Transforms `expr` to `.Discharged` or `.Constant`.
-   This is useful to convert expressions that may either be literals or not
    and get their resulting RK register.

Analogous to:
-   'lcode.c:luaK_exp2RK(FuncState *fs, expdesc *e)' in Lua 5.1.5.

Links:
-    https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2RK

Notes:
-   For most cases, constant values' indexes can safely fit in an RK.
-   However, if that is not true, the constant value must first be pushed to a
    register.
-   Expressions of type `.Constant` will still retain their normal index, so
    the return value is for the caller's use.
 */
compiler_expr_regconst :: proc(compiler: ^Compiler, expr: ^Expr) -> (reg: u16) {
    compiler_expr_to_value(compiler, expr)

    add_rk :: proc(chunk: ^Chunk, value: Value, expr: ^Expr) -> (rk: u16, ok: bool) {
        if len(chunk.constants) <= MAX_INDEX_RK {
            index := chunk_add_constant(chunk, value)
            expr_set_index(expr, .Constant, index)
            return reg_as_k(cast(u16)index), true
        }
        return INVALID_REG, false
    }

    chunk := compiler.chunk
    #partial switch type := expr.type; type {
    case .Nil:
        return add_rk(chunk, value_make_nil(), expr) or_break
    case .True:
        return add_rk(chunk, value_make_boolean(true), expr) or_break
    case .False:
        return add_rk(chunk, value_make_boolean(false), expr) or_break
    case .Number:
        return add_rk(chunk, value_make_number(expr.number), expr) or_break
    case .Constant:
        // Constant can fit in argument C?
        if index := expr.index; index <= MAX_INDEX_RK {
            return reg_as_k(cast(u16)index)
        } else {
            break
        }
    }
    return compiler_expr_any_reg(compiler, expr)
}

///=============================================================================

///=== INSTRUCTION EMISSION ====================================================


/*
Analogous to:
-   `lcode.c:luaK_nil(FuncState *fs, int from, int n)`
 */
compiler_code_nil :: proc(compiler: ^Compiler, reg, count: u16) {
    assert(count != 0, "Emitting 0 nils is invalid!")

    chunk := compiler.chunk
    pc    := chunk.pc

    // No jumps to current position?
    if pc > compiler.last_jump {
        // Function start and positions already clean?
        if pc == 0 && !compiler.is_print && cast(int)reg >= compiler.active_local {
            return
        }
        // TODO(2025-04-09): Remove `if pc > 0` when `print` is a global function
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
    }

    // No optimization.
    compiler_code_AB(compiler, .Load_Nil, reg, reg + count - 1)
}


/*
Analogous to:
-   `compiler.c:emitReturn()` in the book.

Notes:
-   Like in Lua, all functions call this even if they have explicit returns.
 */
compiler_code_return :: proc(compiler: ^Compiler, reg, n_results: u16) {
    // Add 1 because we want to differentiate from arg B == 0 indicating to return
    // up to top (a.k.a varargs).
    compiler_code_AB(compiler, .Return, reg, n_results + 1)
}

compiler_code_ABC :: proc(compiler: ^Compiler, op: OpCode, a, b, c: u16) -> (pc: int) {
    assert(opcode_info[op].type == .Separate)
    return compiler_code(compiler, inst_make_ABC(op, a, b, c))
}

compiler_code_AB :: proc(compiler: ^Compiler, op: OpCode, a, b: u16) -> (pc: int) {
    assert(opcode_info[op].type == .Separate)
    assert(opcode_info[op].a)
    assert(opcode_info[op].b != .Unused)
    return compiler_code(compiler, inst_make_ABC(op, a, b, 0))
}

compiler_code_ABx :: proc(compiler: ^Compiler, op: OpCode, reg: u16, index: u32) -> (pc: int) {
    assert(opcode_info[op].type == .Unsigned_Bx || opcode_info[op].type == .Signed_Bx)
    assert(opcode_info[op].c == .Unused)
    return compiler_code(compiler, inst_make_ABx(op, reg, index))
}

/*
Analogous to:
-   'lcode.c:luaK_code' in Lua 5.1.5.
-   'compiler.c:emitByte()' and 'compiler.c:emitBytes()' in the book.

TODO(2025-01-07):
-   Fix the line counter for folded constant expressions across multiple lines?
 */
compiler_code :: proc(compiler: ^Compiler, inst: Instruction) -> (pc: int) {
    vm    := compiler.vm
    chunk := compiler.chunk
    return chunk_append(vm, chunk, inst, compiler.parser.consumed.line)
}

///=============================================================================


/*
Analogous to:
-   'compiler.c:makeConstant()' in the book.
 */
compiler_add_constant :: proc(compiler: ^Compiler, constant: Value) -> (index: u32) {
    index = chunk_add_constant(compiler.chunk, constant)
    if index >= MAX_CONSTANTS {
        parser_error_consumed(compiler.parser, "Function uses too many constants")
    }
    return index
}


/*
Analogous to:
-   `compiler.c:addLocal()` in the book.
-   `lparser.c:registerlocalvar(LexState *ls, TString *varname)` in Lua 5.1.5.

Links:
-   https://www.lua.org/source/5.1/lparser.c.html#registerlocalvar
 */
compiler_add_local :: proc(compiler: ^Compiler, ident: ^OString) -> (index: u16) {
    i, ok := chunk_add_local(compiler.chunk, ident)
    if !ok {
        parser_error_consumed(compiler.parser, "Too many local variables")
    }
    return i
}


/*
Notes:
-   See `lparser.c:searchvar(FuncState *fs, TString *n)`.
 */
compiler_resolve_local :: proc(compiler: ^Compiler, ident: ^OString) -> (index: u16, ok: bool) {
    return chunk_resolve_local(compiler.chunk, ident, compiler.active_local)
}


/*
Notes:
-   Assumes the target operand was just consumed and now resides in `Expr`.
 */
compiler_code_not :: proc(compiler: ^Compiler, expr: ^Expr) {
    /*
    Details:
    -   In the call to `parser.odin:unary()`, we should have consumed the
        target operand beforehand.
    -   Thus, it should be able to be discharged (if it is not already!)
    -   This means we should be able to pop it as well.
     */
    compiler_discharge_vars(compiler, expr)
    when USE_CONSTANT_FOLDING {
        #partial switch expr.type {
        case .Nil, .False:
            expr.type = .True
            return
        case .True, .Number, .Constant:
            expr.type = .False
            return
        // Registers have runtime values so we can't necessarily fold them.
        case .Discharged, .Need_Register:
            break
        case: unreachable()
        }
    }
    compiler_discharge_any_reg(compiler, expr)
    compiler_expr_pop(compiler, expr^)

    expr_set_pc(expr, .Need_Register, compiler_code_AB(compiler, .Not, 0, expr.reg))
}


/*
Notes:
-   when `!USE_CONSTANT_FOLDING`, we expect that if `left` was a literal then
    we called `compiler_expr_regconst()` beforehand.
-   Otherwise, the order of arguments will be reversed!
-   Comparison expressions of non-number-literals are NEVER folded, because
    checking for equality or inequality is very involved especially when we have
    to resolve constants.

Links:
-   https://www.lua.org/source/5.1/lcode.c.html#codearith
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_posfix
 */
compiler_code_binary :: proc(compiler: ^Compiler, op: OpCode, left, right: ^Expr) {
    assert(.Add <= op && op <= .Unm || .Eq <= op && op <= .Geq || op == .Concat || op == .Len)
    when USE_CONSTANT_FOLDING {
        if compiler_fold_numeric(op, left, right) {
            return
        }
    }

    b, c: u16
    // Right is unused.
    if op == .Unm || op == .Len {
        b = compiler_expr_regconst(compiler, left)
    } else {
        c = compiler_expr_regconst(compiler, right)
        b = compiler_expr_regconst(compiler, left)
    }

    // In the event BOTH are .Discharged, we want to pop them in the correct
    // order! Otherwise the assert in `compiler_pop_reg()` will fail.
    if b > c {
        compiler_expr_pop(compiler, left^)
        compiler_expr_pop(compiler, right^)
    } else {
        compiler_expr_pop(compiler, right^)
        compiler_expr_pop(compiler, left^)
    }

    // Argument A will be fixed down the line.
    expr_set_pc(left, .Need_Register, compiler_code_ABC(compiler, op, 0, b, c))
}


/*
Links:
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_posfix
 */
compiler_code_concat :: proc(compiler: ^Compiler, left, right: ^Expr) {
    compiler_expr_to_value(compiler, right)

    code := compiler.chunk.code[:]

    // This is past the first consecutive concat, so we can fold them.
    if right.type == .Need_Register && code[right.pc].op == .Concat {
        instr := &code[right.pc]
        assert(left.reg == instr.b - 1)
        compiler_expr_pop(compiler, left^)
        instr.b = left.reg
        expr_set_pc(left, .Need_Register, right.pc)
        return
    }
    // This is the first in a potential chain of concats.
    compiler_expr_next_reg(compiler, right)
    compiler_code_binary(compiler, .Concat, left, right)
}


/*
Analogous to:
-   `lcode.c:constfolding(OpCode op, expdesc *e1, expdesc *e2)`

Links:
-   https://www.lua.org/source/5.1/lcode.c.html#constfolding
 */
@(private="file")
compiler_fold_numeric :: proc(op: OpCode, left, right: ^Expr) -> (ok: bool) {
    // Can't fold two non-number-literals!
    if !expr_is_number(left^) || !expr_is_number(right^) {
        return false
    }

    x, y: f64 = left.number, right.number
    result: union #no_nil {f64, bool}
    #partial switch op {
    // Arithmetic
    case .Add:  result = number_add(x, y)
    case .Sub:  result = number_sub(x, y)
    case .Mul:  result = number_mul(x, y)
    case .Div:
        // Avoid division by zero
        if y == 0 {
            return false
        }
        result = number_div(x, y)
    case .Mod:
        // Ditto
        if y == 0 {
            return false
        }
        result = number_mod(x, y)
    case .Pow:  result = number_pow(x, y)
    case .Unm:  result = number_unm(x)

    // Comparison
    case .Eq:   result = number_eq(x, y)
    case .Neq:  result = !number_eq(x, y)
    case .Lt:   result = number_lt(x, y)
    case .Leq:  result = number_leq(x, y)
    case .Gt:   result = number_gt(x, y)
    case .Geq:  result = number_geq(x, y)

    // Cannot optimize concat at compile-time due to string conversion
    case .Concat, .Len: return false
    case: unreachable()
    }

    switch value in result {
    case f64:
        if number_is_nan(value) {
            return false
        }
        expr_set_number(left, value)
        return true
    case bool:
        expr_set_boolean(left, value)
        return true
    case:
        unreachable()
    }
}


/*
Overview
-   Transform `table`, likely of type `.Discharged`, to `.Table_Index`.
    The `table.reg` remains the same but `table.aux` is added to specify
    the index register.

Analogous to:
-   `lcode.c:luaK_indexed(FuncState *fs, expdesc *t, expdesc *key)` in Lua 5.1.5.
 */
compiler_code_indexed :: proc(compiler: ^Compiler, table, key: ^Expr) {
    index := compiler_expr_regconst(compiler, key)
    expr_set_table(table, .Table_Index, index)
}


/*
Analogous to:
-   `lcode.c:luaK_setlist(FuncState *fs, int base, int nelems, int tostore)`
    in Lua 5.1.5.

Assumptions:
-   `total` and `to_store` are never 0.
 */
compiler_code_set_array :: proc(compiler: ^Compiler, reg: u16, total, to_store: int) {
    // Assert taken from Lua 5.4
    assert(to_store != 0 && to_store <= FIELDS_PER_FLUSH)

    // TOOD(2025-04-15): Check for LUA_MULTRET analog?
    b := cast(u16)to_store

    // Add 1 to distinguish from C == 0 which is a special case
    c := ((total - 1) / FIELDS_PER_FLUSH) + 1

    if c <= MAX_C {
        compiler_code_ABC(compiler, .Set_Array, reg, b, cast(u16)c)
    } else {
        parser_error_consumed(compiler.parser,
            ".Set_Array with > MAX_C offset is not yet supported")
        // Actual value of C is found in the next "instruction"
        // compiler_code_ABC(compiler, .Set_Array, reg, b, 0)
        // compiler_code(compiler, transmute(Instruction)cast(u32)c)
    }
    // Reuse the registers that were allocated for the list elements
    compiler.free_reg = cast(int)reg + 1
}
