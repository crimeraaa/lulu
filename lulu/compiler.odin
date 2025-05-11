#+private
package lulu

@require import "core:fmt"
import "base:builtin"
import "core:log"
import "core:container/small_array"

MAX_LOCALS    :: 200
MAX_CONSTANTS :: MAX_Bx

// In the context of jumps, since pc is always the *next* instruction,
// adding `-1` to it simply brings us back to the `Jump` instruction.
// This results in an infinite loop.
NO_JUMP :: -1

Active_Locals :: small_array.Small_Array(MAX_LOCALS, u16)

@cold
unreachable :: #force_inline proc(format: string, args: ..any, location := #caller_location) -> ! {
    when ODIN_DEBUG {
        fmt.panicf(format, ..args, loc = location)
    } else {
        builtin.unreachable()
    }
}

Compiler :: struct {
    // Indexes are local registers. Values are indexes into `chunk.locals[]` for
    // information like identifiers and depth.
    // See `lparser.h:LexState::actvar[]`.
    active:      Active_Locals,
    count:       struct {constants, locals: int},
    vm:         ^VM,
    parent:     ^Compiler, // Enclosing state.
    parser:     ^Parser,   // All nested compilers share the same parser.
    chunk:      ^Chunk,    // Compilers do not own their chunks. They merely fill them.
    scope_depth: int,      // How far down is our current lexical scope?
    free_reg:    int,      // Index of the first free register.
    last_target: int,      // pc of the last jump target. See `FuncState::lasttarget`.
    list_jump:   int,      // List of pending chunks to `chunk.pc`. See `FuncState::jpc`.
    is_print:    bool,     // HACK(2025-04-09): until `print` is a global runtime function
}

compiler_init :: proc(compiler: ^Compiler, vm: ^VM, parser: ^Parser, chunk: ^Chunk) {
    compiler.vm           = vm
    compiler.parser      = parser
    compiler.chunk       = chunk
    compiler.last_target = NO_JUMP
    compiler.list_jump   = NO_JUMP
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
    compiler_code_return(compiler, 0, 0)
    compiler_end_scope(compiler)
    compiler_end(compiler)
}

compiler_end :: proc(compiler: ^Compiler) {
    vm     := compiler.vm
    chunk  := compiler.chunk
    chunk_fini(vm, chunk, compiler)
    when DEBUG_PRINT_CODE {
        debug_dump_chunk(chunk)
    }
}

compiler_begin_scope :: proc(compiler: ^Compiler) {
    compiler.scope_depth += 1
}

/*
**Analogous to**
-   `lparser.c:removevars(LexState *ls, int tolevel)` in Lua 5.1.5.
 */
compiler_end_scope :: proc(compiler: ^Compiler) {
    compiler.scope_depth -= 1
    chunk  := compiler.chunk
    depth  := compiler.scope_depth
    reg    := small_array.len(compiler.active) - 1
    active := small_array.slice(&compiler.active)
    locals := chunk.locals[:compiler.count.locals]
    endpc  := chunk.pc

    // Don't pop registers as we'll go below the active count!
    for reg >= 0 {
        index := active[reg]
        local := &locals[index]
        if local.depth <= depth {
            break
        }
        small_array.pop_back(&compiler.active)
        local.endpc = endpc
        reg -= 1
    }
    compiler.free_reg = reg + 1
}


///=== REGISTER EMISSSION ================================================== {{{


/*
**Analogous to**
-   `lcode.c:luaK_reserveregs(FuncState *fs, int reg)` in Lua 5.1.5.

**Links**
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
    if !reg_is_k(reg) && cast(int)reg >= small_array.len(compiler.active) {
        compiler.free_reg -= 1
        log.assertf(cast(int)reg == compiler.free_reg, "free_reg := %i but reg := %i",
            compiler.free_reg, reg, loc = location)
    }
}


/*
**Assumptions**
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
**Analogous to**
-   `lcode.c:luaK_exp2anyreg(FuncState *fs, expdesc *e)` in Lua 5.1.5.

**Guarantees**
-   `expr` ALWAYS transitions to `.Discharged` if it is not already so.
-   Even if `expr` is a literal or a constant, it will be pushed.
-   If `expr` is already `.Discharged`, we simply return its register.
-   Use `expr.reg` to access which register it is currently in.

**Returns**
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
**Overview**
-   Pushes `expr` to the top of the stack no matter what.

**Analogous to**
-   `lcode.c:luaK_exp2nextreg(FuncState *fs, expdesc *e)` in Lua 5.1.5.

**Guarantees**
-   `expr` will be transformed to type `.Discharged` if it is not so already.
-   Even if `expr` is `.Discharged`, it will be pushed regardless.

**Links**
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2nextreg
 */
compiler_expr_next_reg :: proc(compiler: ^Compiler, expr: ^Expr, location := #caller_location) {
    compiler_discharge_vars(compiler, expr, location = location)
    compiler_expr_pop(compiler, expr^, location = location)

    compiler_reserve_reg(compiler, 1)
    expr_to_reg(compiler, expr, cast(u16)compiler.free_reg - 1)
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
@(private="file")
expr_to_reg :: proc(compiler: ^Compiler, expr: ^Expr, reg: u16) {
    need_value :: proc(compiler: ^Compiler, list: int) -> bool {
        for list := list; list != NO_JUMP; list = get_jump(compiler, list) {
            ip := get_jump_control(compiler, list)
            // Test can produce a value, e.g. `OpCode.Eq`?
            if ip.op != .Test_Set {
                return true
            }
        }
        // None found
        return false
    }

    get_jump :: proc(compiler: ^Compiler, pc: int) -> (offset: int) {
        ip := compiler.chunk.code[pc]
        // Start of jump list?
        if offset = ip_get_sBx(ip); offset == NO_JUMP {
            return NO_JUMP
        }
        // Turn relative offset into absolute position.
        return (pc + 1) + offset
    }

    get_jump_control :: proc(compiler: ^Compiler, pc: int) -> (ip: ^Instruction) {
        ip = &compiler.chunk.code[pc]
        // Have something before the jump instruction?
        if pc >= 1 && opcode_info[ip.op].is_test {
            return ptr_offset(ip, -1)
        }
        return ip
    }

    discharge_to_reg(compiler, expr, reg)

    // uh oh
    if expr_has_jumps(expr^) {
        unreachable("jumps not yet implemented!")
    }

    // NOTE(2025-04-09): Seemingly redundant but useful when we get to jumps.
    expr^ = expr_make(.Discharged, reg = reg)
}


/*
Overview
-   Very similar to `compiler_discharge_to_reg()`
-   We do nothing if `expr` is already of type `.Discharged`.

**Analogous to**
-    `lcode.c:exp2anyreg(FuncState *fs, expdesc *e)`.
 */
@(private="file")
discharge_any_reg :: proc(compiler: ^Compiler, expr: ^Expr, location := #caller_location) {
    if expr.type != .Discharged {
        compiler_reserve_reg(compiler, 1)
        discharge_to_reg(compiler, expr, cast(u16)compiler.free_reg - 1, location = location)
    }
}


/*
**Overview**
-   Transforms `expr` to `.Discharged` no matter what.
-   This may involve emitting get-operations for literals, constants, globals,
    locals, or table indexes.

**Analogous to**
-    `lcode.c:discharge2reg(FuncState *fs, expdesc *e, int reg)`

**Guarantees**
-   `expr.reg` holds the register we just emitted `expr` to.

**Links**
-   https://www.lua.org/source/5.1/lcode.c.html#discharge2reg
 */
@(private="file")
discharge_to_reg :: proc(compiler: ^Compiler, expr: ^Expr, reg: u16, location := #caller_location) {
    compiler_discharge_vars(compiler, expr, location = location)
    #partial switch expr.type {
    case .Nil:      compiler_code_nil(compiler, reg, 1)
    case .True:     compiler_code_ABC(compiler, .Load_Boolean, reg, 1, 0)
    case .False:    compiler_code_ABC(compiler, .Load_Boolean, reg, 0, 0)
    case .Number:
        index := compiler_add_constant(compiler, value_make(expr.number))
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
        // Nothing to do?
        assert(expr.type == .Empty || expr.type == .Jump)
        return
    }
    // Don't set `expr.patch_*`
    expr.type = .Discharged
    expr.reg  = reg
    // expr^ = expr_make(.Discharged, reg = reg)
}

/*
**Overview**
-   Readies the retrieval of expressions representing variables.
-   For other types of expressions nothing occurs.

**Analogous to**
-   `lcode.c:luaK_dischargevars(FuncState *fs, expdesc *e)` in Lua 5.1.5.

**Guarantees**
-   Bytecode is emitted for globals and tables, but destination registers are
    not yet set. `expr` is transformed to `.Need_Register`.
-   Locals are transformed to `.Discharged` because they already have a known
    register.
 */
compiler_discharge_vars :: proc(compiler: ^Compiler, expr: ^Expr, location := #caller_location) {
    #partial switch type := expr.type; type {
    case .Global:
        pc := compiler_code_ABx(compiler, .Get_Global, 0, expr.index)
        expr^ = expr_make(.Need_Register, pc = pc)
    case .Local:
        // info is already the local register we resolved beforehand.
        expr^ = expr_make(.Discharged, reg = expr.reg)
    case .Table_Index:
        // We can now reuse the registers allocated for the table and index.
        table := expr.table.reg
        key   := expr.table.key_reg
        compiler_pop_reg(compiler, key, location = location)
        compiler_pop_reg(compiler, table, location = location)
        pc := compiler_code_ABC(compiler, .Get_Table, 0, table, key)
        expr^ = expr_make(.Need_Register, pc = pc)
    }
}


/*
**Analogous to**
-   `lcode.c:luaK_exp2val(FuncState *fs, expdesc *e)` in Lua 5.1.5.
 */
compiler_expr_to_value :: proc(compiler: ^Compiler, expr: ^Expr) {
    // if hasjumps(expr) luaK_exp2anyreg(fs, e) else
    compiler_discharge_vars(compiler, expr)
}


/*
**Overview**
-   Transforms `expr` to `.Discharged` or `.Constant`.
-   This is useful to convert expressions that may either be literals or not
    and get their resulting RK register.

**Analogous to:**
-   `lcode.c:luaK_exp2RK(FuncState *fs, expdesc *e)` in Lua 5.1.5.

**Links:**
-    https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2RK

**Notes:**
-   For most cases, constant values' indexes can safely fit in an RK.
-   However, if that is not true, the constant value must first be pushed to a
    register.
-   Expressions of type `.Constant` will still retain their normal index, so
    the return value is for the caller's use.
 */
compiler_expr_rk :: proc(compiler: ^Compiler, expr: ^Expr) -> (reg: u16) {
    compiler_expr_to_value(compiler, expr)

    add_rk :: proc(compiler: ^Compiler, value: Value, expr: ^Expr) -> (rk: u16, ok: bool) {
        chunk := compiler.chunk
        if len(chunk.constants) <= MAX_INDEX_RK {
            index := compiler_add_constant(compiler, value)
            expr^ = expr_make(.Constant, index = index)
            return reg_as_k(cast(u16)index), true
        }
        return INVALID_REG, false
    }

    #partial switch type := expr.type; type {
    case .Nil:
        return add_rk(compiler, value_make(), expr) or_break
    case .True:
        return add_rk(compiler, value_make(true), expr) or_break
    case .False:
        return add_rk(compiler, value_make(false), expr) or_break
    case .Number:
        return add_rk(compiler, value_make(expr.number), expr) or_break
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
**Analogous to:**
-   `lcode.c:luaK_nil(FuncState *fs, int from, int n)`
 */
compiler_code_nil :: proc(compiler: ^Compiler, reg, count: u16) {
    assert(count != 0, "Emitting 0 nils is invalid!")

    chunk := compiler.chunk
    pc    := chunk.pc

    // No jumps to current position?
    if pc > compiler.last_target {
        // Since we assume stack frames are initialized to `nil`, we don't need
        // to push `nil` when we're at the very start of the function.
        if pc == 0 && !compiler.is_print && cast(int)reg >= small_array.len(compiler.active) {
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
**Analogous to**
-   `compiler.c:emitReturn()` in Crafting Interpreters, Chapter 17.3:
    *Emitting Bytecode*.

**Notes**
-   Like in Lua, all functions call this even if they have explicit returns.
-   We do not currently handle variadic (vararg) returns.
 */
compiler_code_return :: proc(compiler: ^Compiler, reg, nret: u16) {
    compiler_code_ABC(compiler, .Return, reg, nret, 0)
}

compiler_code_ABC :: proc(compiler: ^Compiler, op: OpCode, a, b, c: u16) -> (pc: int) {
    assert(opcode_info[op].type == .Separate)
    return compiler_code(compiler, ip_make(op, a, b, c))
}

compiler_code_AB :: proc(compiler: ^Compiler, op: OpCode, a, b: u16) -> (pc: int) {
    assert(opcode_info[op].type == .Separate)
    assert(opcode_info[op].a)
    assert(opcode_info[op].b != .Unused)
    return compiler_code(compiler, ip_make(op, a, b, c = 0))
}

compiler_code_ABx :: proc(compiler: ^Compiler, op: OpCode, reg: u16, index: u32) -> (pc: int) {
    assert(opcode_info[op].type == .Unsigned_Bx && opcode_info[op].c == .Unused)
    return compiler_code(compiler, ip_make(op, a = reg, bx = index))
}

compiler_code_AsBx :: proc(compiler: ^Compiler, op: OpCode, reg: u16, jump: int) -> (pc: int) {
    assert(opcode_info[op].type == .Signed_Bx)
    assert(opcode_info[op].c == .Unused)
    return compiler_code(compiler, ip_make(op, a = reg, sbx = jump))
}

/*
**Analogous to**
-   `lcode.c:luaK_code` in Lua 5.1.5.
-   `compiler.c:emitByte()`, `compiler.c:emitBytes()` in Crafting Interpeters,
    Chapter 17.3: "Emitting Bytecode".

**TODO(2025-01-07)**
-   Fix the line counter for folded constant expressions across multiple lines?
 */
compiler_code :: proc(compiler: ^Compiler, inst: Instruction) -> (pc: int) {
    vm    := compiler.vm
    chunk := compiler.chunk
    return chunk_append(vm, chunk, inst, compiler.parser.consumed.line)
}

///=== }}} =====================================================================


/*
**Analogous to**
-   `compiler.c:makeConstant()` in Crafting Interpreters, Chapter 17.4.1:
    *Parsers for tokens*.
 */
compiler_add_constant :: proc(compiler: ^Compiler, constant: Value) -> (index: u32) {
    vm    := compiler.vm
    chunk := compiler.chunk
    count := &compiler.count.constants
    index = chunk_add_constant(vm, chunk, constant, count)
    if index >= MAX_CONSTANTS {
        buf: [64]byte
        msg := fmt.bprintf(buf[:], "More than %i constants", MAX_CONSTANTS)
        parser_error(compiler.parser, msg)
    }
    return index
}


///=== LOCAL MANIPULATION ================================================== {{{


/*
**Analogous to**
-   `compiler.c:addLocal()` in Crafting Interpreters, Chapter 22.3: *Declaring
    Local Variables*.
-   `lparser.c:registerlocalvar(LexState *ls, TString *varname)` in Lua 5.1.5.

**Links**
-   https://www.lua.org/source/5.1/lparser.c.html#registerlocalvar
 */
compiler_add_local :: proc(compiler: ^Compiler, ident: ^OString) -> (index: u16) {
    vm    := compiler.vm
    chunk := compiler.chunk
    count := &compiler.count.locals
    return chunk_add_local(vm, chunk, ident, count)
}


/*
**Notes**
-   See `lparser.c:searchvar(FuncState *fs, TString *n)`.
-   `compiler.chunk.locals` itself contains information about ALL possible
    locals for this chunk.
-   `compiler.active_locals` contains information about which locals in the
    above array are currently in scope.
 */
compiler_resolve_local :: proc(compiler: ^Compiler, ident: ^OString) -> (index: u16, ok: bool) {
    locals := compiler.chunk.locals

    /*
    **Notes**
    -   `active` is NOT what we want to return, as it is an index to
        `chunk.locals`.
    -   `reg` IS what we want to return, as we assume that locals are only
        ever stored sequentially starting from the 0th register.
     */
    #reverse for active, reg in small_array.slice(&compiler.active) {
        // `lparser.c:getlocvar(fs, i)`
        local := locals[active]
        if local.ident == ident {
            return cast(u16)reg, true
        }
    }
    return INVALID_REG, false
}

///=== }}} =====================================================================

///=== OPCODE EMISSION ===================================================== {{{


/*
**Notes**
-   Assumes the target operand was just consumed and now resides in `Expr`.
 */
compiler_code_not :: proc(compiler: ^Compiler, expr: ^Expr) {
    /*
    **Details**
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
        case: unreachable("Cannot fold expression %v", expr.type)
        }
    }
    discharge_any_reg(compiler, expr)
    compiler_expr_pop(compiler, expr^)

    pc := compiler_code_AB(compiler, .Not, 0, expr.reg)
    expr^ = expr_make(.Need_Register, pc = pc)
}


/*
**Notes**
-   when `!USE_CONSTANT_FOLDING`, we expect that if `left` was a literal then
    we called `compiler_expr_rk()` beforehand.
-   Otherwise, the order of arguments will be reversed!
-   Comparison expressions are NEVER folded, because checking for equality or
    inequality is very involved especially when we have to resolve constants.

**Links**
-   https://www.lua.org/source/5.1/lcode.c.html#codearith
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_posfix
 */
compiler_code_arith :: proc(compiler: ^Compiler, op: OpCode, left, right: ^Expr) {
    assert(.Add <= op && op <= .Unm || op == .Concat || op == .Len)
    when USE_CONSTANT_FOLDING {
        if fold_numeric(op, left, right) {
            return
        }
    }

    b, c: u16
    // Right is unused.
    if op == .Unm || op == .Len {
        b = compiler_expr_rk(compiler, left)
    } else {
        c = compiler_expr_rk(compiler, right)
        b = compiler_expr_rk(compiler, left)
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
    pc   := compiler_code_ABC(compiler, op, 0, b, c)
    left^ = expr_make(.Need_Register, pc = pc)
}

/*
**Analogous to**
-   `lcode.c:codecomp(FuncState *fs, OpCode op, int cond, expdesc *e1, expdesc *e2)`
    in Lua 5.1.5.
*/
compiler_code_compare :: proc(compiler: ^Compiler, op: OpCode, inverted: bool, left, right: ^Expr) {
    assert(.Eq <= op && op <= .Leq);
    inverted := inverted
    rkb := compiler_expr_rk(compiler, left)
    rkc := compiler_expr_rk(compiler, right)

    // Reuse these registers after the comparison is made
    compiler_expr_pop(compiler, right^)
    compiler_expr_pop(compiler, left^)

    // Exchange arguments in `<` or `<=` to emulate `>=` or `>`, respectively.
    if inverted && op != .Eq {
        rkb, rkc = rkc, rkb
        inverted = false
    }

    pc := compiler_code_ABC(compiler, op, 0, rkb, rkc)
    left^ = expr_make(.Need_Register, pc = pc)
}

/*
**Links**
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_posfix
 */
compiler_code_concat :: proc(compiler: ^Compiler, left, right: ^Expr) {
    compiler_expr_to_value(compiler, right)

    code := compiler.chunk.code[:compiler.chunk.pc]

    // This is past the first consecutive concat, so we can fold them.
    if right.type == .Need_Register && code[right.pc].op == .Concat {
        ip := &code[right.pc]
        assert(left.reg == ip.b - 1)
        compiler_expr_pop(compiler, left^)
        ip.b  = left.reg
        left^ = expr_make(.Need_Register, pc = right.pc)
        return
    }
    // This is the first in a potential chain of concats.
    compiler_expr_next_reg(compiler, right)
    compiler_code_arith(compiler, .Concat, left, right)
}


/*
**Analogous to**
-   `lcode.c:constfolding(OpCode op, expdesc *e1, expdesc *e2)`

Links:
-   https://www.lua.org/source/5.1/lcode.c.html#constfolding
 */
@(private="file")
fold_numeric :: proc(op: OpCode, left, right: ^Expr) -> (ok: bool) {
    // Can't fold two non-number-literals!
    if !expr_is_number(left^) || !expr_is_number(right^) {
        return false
    }

    x, y: f64 = left.number, right.number
    result: f64
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

    // Cannot optimize concat at compile-time due to string conversion
    case .Concat, .Len: return false
    case: unreachable("Cannot fold opcode %v", op)
    }

    if number_is_nan(result) {
        return false
    }
    left^ = expr_make(.Number, result)
    return true
}


/*
**Overview**
-   Transform `table`, likely of type `.Discharged`, to `.Table_Index`.
    The `table.reg` remains the same but `table.aux` is added to specify
    the index register.

**Analogous to**
-   `lcode.c:luaK_indexed(FuncState *fs, expdesc *t, expdesc *key)` in Lua 5.1.5.
 */
compiler_code_indexed :: proc(compiler: ^Compiler, table, key: ^Expr) {
    index := compiler_expr_rk(compiler, key)
    table^ = expr_make(.Table_Index, reg = table.reg, index = index)
}


/*
**Analogous to**
-   `lcode.c:luaK_setlist(FuncState *fs, int base, int nelems, int tostore)`
    in Lua 5.1.5.

**Assumptions**
-   `total` and `to_store` are never 0.
 */
compiler_code_set_array :: proc(compiler: ^Compiler, reg: u16, total, to_store: int) {
    // Assert taken from Lua 5.4
    assert(to_store != 0 && to_store <= FIELDS_PER_FLUSH)

    // TOOD(2025-04-15): Check for LUA_MULTRET analog?
    b := cast(u16)to_store

    /*
    **Notes (2025-04-18)**
    -   We subtract 1 in case `total == FIELDS_PER_FLUSH` which would result in
        C == 2. Otherwise, it will be decoded as the wrong offset!
    -   We add 1 to distinguish from C == 0 which is a special case.
     */
    c := ((total - 1) / FIELDS_PER_FLUSH) + 1

    if c <= MAX_C {
        compiler_code_ABC(compiler, .Set_Array, reg, b, cast(u16)c)
    } else {
        parser_error(compiler.parser,
            ".Set_Array with > MAX_C offset is not yet supported")
        // Actual value of C is found in the next "instruction"
        // compiler_code_ABC(compiler, .Set_Array, reg, b, 0)
        // compiler_code(compiler, transmute(Instruction)cast(u32)c)
    }
    // Reuse the registers that were allocated for the list elements
    compiler.free_reg = cast(int)reg + 1
}


/*
**Overview**
-   Emits the bytecode needed to set global/local variables and table fields.

**Analogous to**
-   `lcode.c:luaK_storevar(FuncState *fs, expdesc *v, expdesc *e)` in Lua 5.1.5.

**Assumptions**
-   See notes per switch-case.
-   `expr` can be pushed, reused as a local, or used as a constant index.
 */
compiler_store_var :: proc(compiler: ^Compiler, var, expr: ^Expr) {
    #partial switch var.type {
    /*
    -   Most likely we want to do `OpCode.Move` as this is usually the case for
        assignment statements.
    -   Of course if you are assigning a local to itself, e.g.
        `local x = 1; x = x;` then nothing happens. This is an optimization.
     */
    case .Local:
        // If `expr` is currently a non-local register, reuse its register
        compiler_expr_pop(compiler, expr^)
        expr_to_reg(compiler, expr, var.reg)
    /*
    -   `var.index` is already set to the identifier.
    -   `expr` can only be pushed to a register (constants) or reuse an
        existing register (locals, temporaries).
     */
    case .Global:
        reg   := compiler_expr_any_reg(compiler, expr)
        index := var.index
        compiler_code_ABx(compiler, .Set_Global, reg, index)
    /*
    -   `var.table` already refers to the target table and key.
    -   `expr` can be emitted to an RK register as is defined in `opcode.odin`.
    -   This allows us to optimize by not needing to push constants to the stack
        beforehand.
     */
    case .Table_Index:
        table := var.table
        value := compiler_expr_rk(compiler, expr)
        compiler_code_ABC(compiler, .Set_Table, table.reg, table.key_reg, value)
    case:
        unreachable("Invalid variable kind %v", var.type)
    }
    // If `expr` ended up as a non-local register, reuse its register
    compiler_expr_pop(compiler, expr^)
}

///=== }}} =====================================================================

///=== JUMP MANIPULATION =================================================== {{{


/*
**Overview**
-   Emit the `.Test` opcode along with its necessary `.Jump`.

**Guarantees**
-   Argument A will use the register/constant of `expr`.
-   If `expr` is a non-local register then it is popped.
-   `expr` will always be transformed to type `.Discharged`.

**Returns**
-   The pc of the `.Jump` we emitted.
 */
compiler_code_test :: proc(compiler: ^Compiler, expr: ^Expr, cond: bool) -> (jump_pc: int) {
    ra := compiler_expr_any_reg(compiler, expr)
    compiler_expr_pop(compiler, expr^)
    return compiler_code_cond_jump(compiler, .Test, ra, 0, u16(cond))
}


/*
**Analogous to**
-   `lcode.c:condjump(FuncState *fs, OpCode op, int A, int B, int C)` in
    Lua 5.1.5.
*/
compiler_code_cond_jump :: proc(compiler: ^Compiler, op: OpCode, a, b, c: u16) -> (jump_pc: int) {
    compiler_code_ABC(compiler, op, a, b, c)
    return compiler_code_jump(compiler)
}


/*
**Overview**
-   Emit `.Test_Set` along with its associated `.Jump`.
-   `left` is emitted and reused to be the potential destination register.

**Guarantees**
-   `left` is first pushed to some register if it is not one already.
-   If `left` is a non-local register, then it is popped.
-   `left` is then transformed to type `.Need_Register` with its `pc` pointing
    to `.Test_Set`.
*/
compiler_code_test_set :: proc(compiler: ^Compiler, left: ^Expr, cond: bool) -> (jump_pc: int) {
    reg := compiler_expr_any_reg(compiler, left)
    compiler_expr_pop(compiler, left^)

    test_pc := compiler_code_ABC(compiler, .Test_Set, 0, reg, u16(cond))
    left^ = expr_make(.Need_Register, pc = test_pc)
    return compiler_code_jump(compiler)
}


/*
**Analogous to**
-   `compiler.c:emitJump(uint8_t instruction)` in Crafting Interpreters,
    Chapter 23.1: *If Statements*.

**Assumptions**
-   `prev` is the pc of the previous jump we want to chain.
-   If it's `NO_JUMP` then that indicates this is the very first jump in the
    chain.

**Returns**
-   The index of our jump instruction in the current chunk's code.
*/
compiler_code_jump :: proc(compiler: ^Compiler, prev := NO_JUMP) -> (jump_pc: int) {
    return compiler_code_AsBx(compiler, .Jump, 0, prev)
}


/*
**Analogous to**
-   `compiler.c:patchJump(int offset)` in Crafting Interpreters, Chapter 23.1:
    *If Statements*.
-   `lcode.c:patchlistaux(FuncState *fs, int list, int vtarget, int reg, int dtarget)` in Lua 5.1.5.
*/
compiler_patch_jump :: proc(compiler: ^Compiler, jump_pc: int) {
    chunk := compiler.chunk
    code  := chunk.code
    pc    := chunk.pc
    for list := jump_pc; list != NO_JUMP; {
        ip     := &code[list]
        offset := pc - list - 1
        if !(-MAX_sBx <= offset && offset <= MAX_sBx) {
            parser_error(compiler.parser, "Jump too large")
        }

        // If ip.sBx is not `NO_JUMP` then we still have a chain to resolve.
        list = ip_get_sBx(ip^)
        ip_set_sBx(ip, offset)
    }
}


///=== }}} =====================================================================
