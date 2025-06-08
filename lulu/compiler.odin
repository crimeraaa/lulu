#+private
package lulu

import "core:fmt"
import "base:builtin"
import "core:container/small_array"

MAX_LOCALS    :: 200
MAX_CONSTANTS :: MAX_Bx

// In the context of jumps, since pc is always the *next* instruction,
// adding `-1` to it simply brings us back to the `Jump` instruction.
// This results in an infinite loop.
NO_JUMP :: -1

Active_Locals :: small_array.Small_Array(MAX_LOCALS, u16)

@cold
unreachable :: #force_inline proc(format: string, args: ..any, loc := #caller_location) -> ! {
    when ODIN_DEBUG {
        fmt.panicf(format, ..args, loc = loc)
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
    free_reg:    int,      // Index of the first free register.
    last_target: int,      // pc of the last jump target. See `FuncState::lasttarget`.
    pc:          int,      // First free index in `chunk.code`.
}

get_ip :: proc {
    get_ip_from_expr,
    get_ip_from_pc,
}

get_ip_from_expr :: #force_inline proc "contextless" (c: ^Compiler, e: ^Expr) -> ^Instruction {
    return &c.chunk.code[e.pc]
}

get_ip_from_pc :: #force_inline proc "contextless" (c: ^Compiler, pc: int) -> ^Instruction {
    return &c.chunk.code[pc]
}

compiler_init :: proc(vm: ^VM, c: ^Compiler, parser: ^Parser, chunk: ^Chunk) {
    c.vm          = vm
    c.parser      = parser
    c.chunk       = chunk
    c.last_target = NO_JUMP
}

compiler_end :: proc(c: ^Compiler) {
    /*
    **Notes** (2025-05-17)
    -   We cannot assume we don't need the implicit return.
    -   Concept check: `local x; if x then return x end`
    -   The last instruction *is* a return, but only conditionally.
    -   Say the `if` branch fails; our next instruction is invalid.
     */
    compiler_code_return(c, reg = 0, count = 0)
    chunk_fini(c.vm, c.chunk, c)
    if DEBUG_PRINT_CODE {
        debug_dump_chunk(c.chunk, len(c.chunk.code))
    }
}


///=== REGISTER EMISSSION ================================================== {{{


/*
**Analogous to**
-   `lcode.c:luaK_reserveregs(FuncState *fs, int reg)` in Lua 5.1.5.

**Links**
-    https://www.lua.org/source/5.1/lcode.c.html#luaK_reserveregs
 */
compiler_reg_reserve :: proc(c: ^Compiler, count: int) {
    c.free_reg += count
    if c.free_reg > c.chunk.stack_used {
        c.chunk.stack_used = c.free_reg
    }
}


compiler_reg_pop :: proc(c: ^Compiler, reg: u16) {
    // Only pop if nonconstant and not the register of an existing local.
    if !reg_is_k(reg) && cast(int)reg >= small_array.len(c.active) {
        c.free_reg -= 1
        fmt.assertf(cast(int)reg == c.free_reg,
            "c.free_reg := %i but reg := %i @ %s:%i",
            c.free_reg, reg, c.chunk.source, c.parser.consumed.line)
    }
}


/*
**Assumptions**
-   If `e` is `.Discharged`, and its register does not refer to an *existing*
    local, it MUST be the most recently discharged register in order to be able
    to pop it.
 */
compiler_expr_pop :: proc(c: ^Compiler, e: Expr) {
    // if e->k == VNONRELOC
    if e.type == .Discharged {
        compiler_reg_pop(c, e.reg)
    }
}


/*
**Analogous to**
-   `lcode.c:luaK_exp2anyreg(FuncState *fs, expdesc *e)` in Lua 5.1.5.

**Guarantees**
-   `e` ALWAYS transitions to `.Discharged` if it is not already so.
-   Even if `e` is a literal or a constant, it will be pushed.
-   If `e` is already `.Discharged`, we simply return its register.
-   Use `e.reg` to access which register it is currently in.

**Returns**
-   The register we stored `e` in.

Links
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2anyreg
 */
compiler_expr_any_reg :: proc(c: ^Compiler, e: ^Expr) -> (reg: u16) {
    compiler_discharge_vars(c, e)

    // If already in a register don't waste time trying to re-emit it.
    // Doing so will also mess up any calls to 'compiler_pop_reg()'.
    if e.type == .Discharged {
        // Already have a register.
        if !expr_has_jumps(e^) {
            return e.reg
        }

        // Register, which is part of a patch list, is not a local?
        if e.reg >= cast(u16)small_array.len(c.active) {
            expr_to_reg(c, e, e.reg)
            return e.reg
        }
    }
    compiler_expr_next_reg(c, e)
    return e.reg
}


/*
**Overview**
-   Pushes `e` to the top of the stack no matter what.

**Analogous to**
-   `lcode.c:luaK_exp2nextreg(FuncState *fs, expdesc *e)` in Lua 5.1.5.

**Guarantees**
-   `e` will be transformed to type `.Discharged` if it is not so already.
-   Even if `e` is `.Discharged`, it will be pushed regardless.

**Links**
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_exp2nextreg
 */
compiler_expr_next_reg :: proc(c: ^Compiler, e: ^Expr) {
    compiler_discharge_vars(c, e)
    compiler_expr_pop(c, e^)

    compiler_reg_reserve(c, 1)
    expr_to_reg(c, e, cast(u16)c.free_reg - 1)
}


/*
Overview
-   Assigns `e` to register `reg`. `e` will always be transformed into
    type `.Discharged`.
-   Typically only called by other functions which simply want to push to the
    index of the first free register `c.free_reg`.

Analogous to
-   `lcode.c:exp2reg(FuncState *fs, expdesc *e, int reg)` in Lua 5.1.5
 */
@(private="file")
expr_to_reg :: proc(c: ^Compiler, e: ^Expr, reg: u16) {
    // Determine if the instruction before the `.Jump` found at `pc` will
    // produce a value (e.g. comparisons need to load bools).
    need_value :: proc(c: ^Compiler, pc: int) -> bool {
        for list := pc; list != NO_JUMP; {
            next := get_jump(c, list)
            if ip := get_jump_control(c, list); ip.op != .Test_Set {
                return true
            }
            list = next
        }
        // No value needed; `.Test_Set` uses register A for that.
        return false
    }

    code_label :: proc(c: ^Compiler, a: u16, b, skip: bool) -> (pc: int) {
        // comparisons themselves may be jump targets
        compiler_get_label(c)
        return compiler_code_boolean(c, a, b, skip)
    }

    discharge_to_reg(c, e, reg)
    is_jump := e.type == .Jump
    if is_jump {
        // use `pc` of the comparison/test as the first jump when true
        compiler_add_jump(c, &e.patch_true, e.pc)
    }

    // `discharge_to_reg()` didn't modify `e` so let's take care of it here
    if expr_has_jumps(e^) {
        load_false := NO_JUMP
        load_true  := NO_JUMP
        if need_value(c, e.patch_true) || need_value(c, e.patch_false) {
            // `Expr` that are not explicitly `.Jump` can still have pending
            // patch lists!
            jump_to_false := NO_JUMP if is_jump else compiler_code_jump(c)
            load_false = code_label(c, reg, b = false, skip = true)
            load_true  = code_label(c, reg, b = true,  skip = false)
            // Don't immediately patch because we don't want to call
            // `get_label()` if it's not a jump.
            if jump_to_false != NO_JUMP {
                compiler_patch_jump(c, jump_to_false)
            }
        }
        compiler_patch_jump(c, e.patch_false, target = load_false, reg = reg)
        compiler_patch_jump(c, e.patch_true,  target = load_true,  reg = reg)
    }

    /*
    **Notes** (2025-05-12)
    -   Use `expr_make*` since we DO want to reset the jump lists at this
        point; we just discharged them.
     */
    e^ = expr_make(.Discharged, reg = reg)
}


/*
**Overview**
-   Mark `c.n_code` as a jump target to avoid wrong optimizations, mainly when
    we have consecutive instructions not in the same basic block.

**Analogous to**
-   `lcode.c:luaK_getlabel(FuncState *fs)` in Lua 5.1.5.

**Note** (2025-05-12):
-   This seems to only apply to `compiler_code_nil()`.
 */
compiler_get_label :: proc(c: ^Compiler) -> (pc: int) {
    pc = c.pc
    c.last_target = pc
    return pc
}


/*
**Overview**
-   Push `e` to the next register if it's not already of type `.Discharged`.
-   `e` is guaranteed to be transformed to `.Discharged`, except if it's
    of type `.Empty` or `.Jump`.

**Analogous to**
-    `lcode.c:exp2anyreg(FuncState *fs, expdesc *e)` in Lua 5.1.5.
 */
@(private="file")
discharge_any_reg :: proc(c: ^Compiler, e: ^Expr) {
    if e.type != .Discharged {
        compiler_reg_reserve(c, 1)
        discharge_to_reg(c, e, cast(u16)c.free_reg - 1)
    }
}


/*
**Overview**
-   Transforms `e` to `.Discharged` no matter what.
-   This may involve emitting get-operations for literals, constants, globals,
    locals, or table indexes.

**Analogous to**
-    `lcode.c:discharge2reg(FuncState *fs, expdesc *e, int reg)`

**Guarantees**
-   `e.reg` holds the register we just emitted `e` to.

**Links**
-   https://www.lua.org/source/5.1/lcode.c.html#discharge2reg
 */
@(private="file")
discharge_to_reg :: proc(c: ^Compiler, e: ^Expr, reg: u16) {
    compiler_discharge_vars(c, e)
    #partial switch e.type {
    case .Nil:      compiler_code_nil(c, reg, 1)
    case .True:     compiler_code_boolean(c, reg, true)
    case .False:    compiler_code_boolean(c, reg, false)
    case .Number:
        index := compiler_add_constant(c, e.number)
        compiler_code(c, .Load_Constant, reg = reg, index = index)
    case .Constant:
        compiler_code(c, .Load_Constant, reg = reg, index = e.index)
    case .Need_Register: get_ip(c, e).a = reg
    case .Discharged:
        // e.g. getting a local variable
        if reg != e.reg {
            compiler_code(c, .Move, ra = reg, rb = e.reg)
        }
    case:
        // Nothing to do?
        assert(e.type == .Empty || e.type == .Jump)
        return
    }
    expr_set(e, .Discharged, reg = reg)
}

/*
**Overview**
-   Readies the retrieval of expressions representing variables.
-   For other types of expressions nothing occurs.

**Analogous to**
-   `lcode.c:luaK_dischargevars(FuncState *fs, expdesc *e)` in Lua 5.1.5.

**Guarantees**
-   Bytecode is emitted for globals and tables, but destination registers are
    not yet set. `e` is transformed to `.Need_Register`.
-   Locals are transformed to `.Discharged` because they already have a known
    register.
 */
compiler_discharge_vars :: proc(c: ^Compiler, e: ^Expr) {
    #partial switch type := e.type; type {
    case .Global:
        pc := compiler_code(c, .Get_Global, reg = 0, index = e.index)
        expr_set(e, .Need_Register, pc = pc)
    case .Local:
        // info is already the local register we resolved beforehand.
        expr_set(e, .Discharged, reg = e.reg)
    case .Table_Index:
        table_reg := e.table.reg
        key_reg   := e.table.key_reg
        // We can now reuse the registers allocated for the table and index.
        compiler_reg_pop(c, key_reg)
        compiler_reg_pop(c, table_reg)
        pc := compiler_code(c, .Get_Table, a = 0, b = table_reg, c = key_reg)
        expr_set(e, .Need_Register, pc = pc)
    case .Call:
        compiler_set_one_return(c, e)
    }
}


/*
**Analogous to**
-   `lcode.c:luaK_exp2val(FuncState *fs, expdesc *e)` in Lua 5.1.5.
 */
compiler_expr_to_value :: proc(c: ^Compiler, e: ^Expr) {
    if expr_has_jumps(e^) {
        compiler_expr_any_reg(c, e)
    } else {
        compiler_discharge_vars(c, e)
    }
}


/*
**Overview**
-   Transforms `e` to `.Discharged` or `.Constant`.
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
compiler_expr_rk :: proc(c: ^Compiler, e: ^Expr) -> (reg: u16) {
    compiler_expr_to_value(c, e)

    add_rk :: proc(c: ^Compiler, v: Value, e: ^Expr) -> (rk: u16, ok: bool) {
        if len(c.chunk.constants) <= MAX_INDEX_RK {
            index := compiler_add_constant(c, v)
            expr_set(e, .Constant, index = index)
            return reg_as_k(cast(u16)index), true
        }
        return NO_REG, false
    }

    #partial switch e.type {
    case .Nil:    return add_rk(c, value_make(), e) or_break
    case .True:   return add_rk(c, value_make(true), e) or_break
    case .False:  return add_rk(c, value_make(false), e) or_break
    case .Number: return add_rk(c, value_make(e.number), e) or_break
    case .Constant:
        // Constant can fit in argument C?
        if index := e.index; index <= MAX_INDEX_RK {
            return reg_as_k(cast(u16)index)
        }
    }
    return compiler_expr_any_reg(c, e)
}

///=============================================================================

///=== INSTRUCTION EMISSION ====================================================


compiler_code_boolean :: proc(c: ^Compiler, reg: u16, b: bool, skip := false) -> (pc: int) {
    return compiler_code(c, .Load_Boolean, a = reg, b = u16(b), c = u16(skip))
}

/*
**Analogous to:**
-   `lcode.c:luaK_nil(FuncState *fs, int from, int n)`
 */
compiler_code_nil :: proc(c: ^Compiler, reg, count: u16) {
    assert(count != 0, "Emitting 0 nils is invalid!")

    // No jumps to current position?
    if pc := c.pc; pc > c.last_target {
        // Since we assume stack frames are initialized to `nil`, we don't need
        // to push `nil` when we're at the very start of the function.
        if pc == 0 && cast(int)reg >= small_array.len(c.active) {
            return
        }

        folding: {
            prev := get_ip(c, pc - 1)
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
    compiler_code(c, .Load_Nil, ra = reg, rb = reg + count - 1)
}


/*
**Analogous to**
-   `compiler.c:emitReturn()` in Crafting Interpreters, Chapter 17.3:
    *Emitting Bytecode*.

**Notes**
-   Like in Lua, all functions call this even if they have explicit returns.
-   We do not currently handle variadic (vararg) returns.
 */
compiler_code_return :: proc(c: ^Compiler, reg, count: u16) {
    compiler_code_ABC(c, .Return, reg, count, 0)
}

compiler_code :: proc {
    compiler_code_ABC,
    compiler_code_AB,
    compiler_code_ABx,
    compiler_code_AsBx,
}

compiler_code_ABC :: proc(cl: ^Compiler, op: OpCode, a, b, c: u16) -> (pc: int) {
    assert(opcode_info[op].type == .Separate)
    return add_instruction(cl, ip_make(op, a, b, c))
}

compiler_code_AB :: proc(c: ^Compiler, op: OpCode, ra, rb: u16) -> (pc: int) {
    assert(opcode_info[op].type == .Separate)
    assert(opcode_info[op].a)
    assert(opcode_info[op].b != .Unused)
    assert(opcode_info[op].c == .Unused)
    return add_instruction(c, ip_make(op, ra, rb, 0))
}

compiler_code_ABx :: proc(c: ^Compiler, op: OpCode, reg: u16, index: u32) -> (pc: int) {
    assert(opcode_info[op].type == .Unsigned_Bx)
    assert(opcode_info[op].c == .Unused)
    return add_instruction(c, ip_make(op, a = reg, bx = index))
}

compiler_code_AsBx :: proc(c: ^Compiler, op: OpCode, reg: u16, jump: int) -> (pc: int) {
    assert(opcode_info[op].type == .Signed_Bx)
    assert(opcode_info[op].c == .Unused)
    return add_instruction(c, ip_make(op, a = reg, sbx = jump))
}

/*
**Analogous to**
-   `lcode.c:luaK_code` in Lua 5.1.5.
-   `compiler.c:emitByte()`, `compiler.c:emitBytes()` in Crafting Interpeters,
    Chapter 17.3: "Emitting Bytecode".

**TODO(2025-01-07)**
-   Fix the line counter for folded constant expressions across multiple lines?
 */
@(private="file")
add_instruction :: proc(c: ^Compiler, i: Instruction) -> (pc: int) {
    chunk_append(c.vm, c.chunk, i, c.parser.consumed.line, &c.pc)
    return c.pc - 1
}

///=== }}} =====================================================================


compiler_add_constant :: proc {
    compiler_add_number,
    compiler_add_value,
    compiler_add_string,
    compiler_add_function,
}


compiler_add_number :: proc(c: ^Compiler, n: Number) -> (index: u32) {
    return compiler_add_value(c, value_make(n))
}

compiler_add_string :: proc(c: ^Compiler, s: ^OString) -> (index: u32) {
    return compiler_add_value(c, value_make(s))
}

compiler_add_function :: proc(c: ^Compiler, f: ^Function) -> (index: u32) {
    return compiler_add_value(c, value_make(f))
}


/*
**Analogous to**
-   `compiler.c:makeConstant()` in Crafting Interpreters, Chapter 17.4.1:
    *Parsers for tokens*.
 */
compiler_add_value :: proc(c: ^Compiler, v: Value) -> (index: u32) {
    index = chunk_add_constant(c.vm, c.chunk, v, &c.count.constants)
    if index >= MAX_CONSTANTS {
        buf: [64]byte
        msg := fmt.bprintf(buf[:], "More than %i constants", MAX_CONSTANTS)
        parser_error(c.parser, msg)
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
compiler_add_local :: proc(c: ^Compiler, ident: ^OString) -> (index: u16) {
    return chunk_add_local(c.vm, c.chunk, ident, &c.count.locals)
}


/*
**Analogous to**
-   `lparser.c:searchvar(FuncState *fs, TString *n)` in Lua 5.1.5.

**Notes**
-   `c.chunk.locals` itself contains information about ALL possible
    locals for this chunk.
-   `c.active[]` contains information about which locals in the
    above array are currently in scope.
 */
compiler_resolve_local :: proc(c: ^Compiler, ident: ^OString) -> (index: u16, ok: bool) {
    locals := c.chunk.locals

    /*
    **Notes**
    -   `active` is NOT what we want to return, as it is an index to
        `chunk.locals`.
    -   `reg` IS what we want to return, as we assume that locals are only
        ever stored sequentially starting from the 0th register.
     */
    #reverse for active, reg in small_array.slice(&c.active) {
        // `lparser.c:getlocvar(FuncState *fs, int i)`
        if locals[active].ident == ident {
            return cast(u16)reg, true
        }
    }

    if c.parent != nil {
        index, ok = compiler_resolve_local(c.parent, ident)
        if ok {
            parser_error(c.parser, "Upvalues not yet supported")
        }
    }
    return NO_REG, false
}

///=== }}} =====================================================================

///=== OPCODE EMISSION ===================================================== {{{


/*
**Notes**
-   Assumes the target operand was just consumed and now resides in `Expr`.
 */
compiler_code_not :: proc(c: ^Compiler, e: ^Expr) {
    /*
    **Details**
    -   In the call to `parser.odin:unary()`, we should have consumed the
        target operand beforehand.
    -   Thus, it should be able to be discharged (if it is not already!)
    -   This means we should be able to pop it as well.
     */
    compiler_discharge_vars(c, e)
    when USE_CONSTANT_FOLDING {
        #partial switch e.type {
        case .Nil, .False:
            e.type = .True
            return
        case .True, .Number, .Constant:
            e.type = .False
            return
        case .Jump:
            invert_comparison(c, e^)
            return
        // Registers have runtime values so we can't necessarily fold them.
        case .Discharged, .Need_Register:
            break
        case: unreachable("Cannot fold expression %v", e.type)
        }
    }
    discharge_any_reg(c, e)
    compiler_expr_pop(c, e^)

    pc := compiler_code(c, .Not, ra = 0, rb = e.reg)
    expr_set(e, .Need_Register, pc = pc)
}


when USE_CONSTANT_FOLDING {

/*
**Analogous to**
-   `lcode.c:constfolding(OpCode op, expdesc *e1, expdesc *e2)`

Links:
-   https://www.lua.org/source/5.1/lcode.c.html#constfolding
 */
@(private="file")
fold_arith :: proc(op: OpCode, left, right: ^Expr) -> (success: bool) {
    // Can't fold two non-number-literals!
    if !expr_is_number(left^) || !expr_is_number(right^) {
        return false
    }

    x, y: Number = left.number, right.number
    result: Number
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
    left^ = expr_make(result)
    return true
}

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
compiler_code_arith :: proc(c: ^Compiler, op: OpCode, left, right: ^Expr) {
    assert(.Add <= op && op <= .Unm || op == .Concat || op == .Len)
    when USE_CONSTANT_FOLDING {
        if fold_arith(op, left, right) {
            return
        }
    }

    rkb, rkc: u16
    // Right is unused.
    if op == .Unm || op == .Len {
        rkb = compiler_expr_rk(c, left)
    } else {
        rkc = compiler_expr_rk(c, right)
        rkb = compiler_expr_rk(c, left)
    }

    // In the event BOTH are .Discharged, we want to pop them in the correct
    // order! Otherwise the assert in `compiler_pop_reg()` will fail.
    if rkb > rkc {
        compiler_expr_pop(c, left^)
        compiler_expr_pop(c, right^)
    } else {
        compiler_expr_pop(c, right^)
        compiler_expr_pop(c, left^)
    }

    // Argument A will be fixed down the line.
    pc   := compiler_code(c, op, a = 0, b = rkb, c = rkc)
    left^ = expr_make(.Need_Register, pc = pc)
}

when USE_CONSTANT_FOLDING {

fold_compare :: proc(op: OpCode, left, right: ^Expr, cond: bool) -> (success: bool) {
    if !expr_is_number(left^) || !expr_is_number(right^) {
        if op == .Eq && expr_is_literal(left^) && expr_is_literal(right^) {
            b := (left.type == right.type)
            if !cond {
                b = !b
            }
            left^ = expr_make(b)
            return true
        }
        return false
    }
    a, b := left.number, right.number
    res: bool
    #partial switch op {
    case .Eq:  res = number_eq(a, b)
    case .Lt:  res = number_lt(a, b)
    case .Leq: res = number_leq(a, b)
    case:
        unreachable("Cannot compare using opcode %v", op)
    }
    if !cond {
        res = !res
    }
    left^ = expr_make(res)
    return true
}

}

/*
**Analogous to**
-   `lcode.c:codecomp(FuncState *fs, OpCode op, int cond, expdesc *e1, expdesc *e2)`
    in Lua 5.1.5.
*/
compiler_code_compare :: proc(c: ^Compiler, op: OpCode, cond: bool, left, right: ^Expr) {
    assert(.Eq <= op && op <= .Leq);
    when USE_CONSTANT_FOLDING {
        if fold_compare(op, left, right, cond) {
            return
        }
    }

    cond := cond
    rkb := compiler_expr_rk(c, left)
    rkc := compiler_expr_rk(c, right)

    // Reuse these registers after the comparison is made
    compiler_expr_pop(c, right^)
    compiler_expr_pop(c, left^)

    // Exchange arguments in `<` or `<=` to emulate `>` or `>=`, respectively.
    if !cond && op != .Eq {
        rkb, rkc = rkc, rkb
        cond = !cond
    }

    pc := compiler_code_cond_jump(c, op, u16(cond), rkb, rkc)
    left^ = expr_make(.Jump, pc = pc)
}

/*
**Links**
-   https://www.lua.org/source/5.1/lcode.c.html#luaK_posfix
 */
compiler_code_concat :: proc(c: ^Compiler, left, right: ^Expr) {
    compiler_expr_to_value(c, right)
    // This is past the first consecutive concat, so we can fold them.
    if right.type == .Need_Register {
        if ip := get_ip(c, right); ip.op == .Concat {
            assert(left.reg == ip.b - 1)
            compiler_expr_pop(c, left^)
            ip.b  = left.reg
            left^ = expr_make(.Need_Register, pc = right.pc)
            return
        }
    }
    // This is the first in a potential chain of concats.
    compiler_expr_next_reg(c, right)
    compiler_code_arith(c, .Concat, left, right)
}


/*
**Overview**
-   Transform `table`, likely of type `.Discharged`, to `.Table_Index`.
    The `table.reg` remains the same but `table.aux` is added to specify
    the index register.

**Analogous to**
-   `lcode.c:luaK_indexed(FuncState *fs, expdesc *t, expdesc *key)` in Lua 5.1.5.
 */
compiler_code_indexed :: proc(c: ^Compiler, table, key: ^Expr) {
    index := compiler_expr_rk(c, key)
    expr_set(table, .Table_Index, table_reg = table.reg, key_reg = index)
}


/*
**Analogous to**
-   `lcode.c:luaK_setlist(FuncState *fs, int base, int nelems, int tostore)`
    in Lua 5.1.5.

**Assumptions**
-   `total` and `to_store` are never 0.
 */
compiler_code_set_array :: proc(c: ^Compiler, reg: u16, total, to_store: int) {
    // Assert taken from Lua 5.4
    assert(to_store != 0)
    assert(to_store <= FIELDS_PER_FLUSH)

    // TOOD(2025-04-15): Check for LUA_MULTRET analog?
    count := cast(u16)to_store

    /*
    **Notes (2025-04-18)**
    -   We subtract 1 in case `total == FIELDS_PER_FLUSH` which would result in
        C == 2. Otherwise, it will be decoded as the wrong offset!
    -   We add 1 to distinguish from C == 0 which is a special case.
     */
    if offset := ((total - 1) / FIELDS_PER_FLUSH) + 1; offset <= MAX_C {
        compiler_code(c, .Set_Array, a = reg, b = count, c = cast(u16)offset)
    } else {
        parser_error(c.parser,
            ".Set_Array with > MAX_C offset is not yet supported")
        // Actual value of C is found in the next "instruction"
        // compiler_code_ABC(c, .Set_Array, reg, b, 0)
        // compiler_code(c, transmute(Instruction)cast(u32)c)
    }
    // Reuse the registers that were allocated for the list elements
    c.free_reg = cast(int)reg + 1
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
compiler_store_var :: proc(c: ^Compiler, var, expr: ^Expr) {
    #partial switch var.type {
    /*
    -   Most likely we want to do `OpCode.Move` as this is usually the case for
        assignment statements.
    -   Of course if you are assigning a local to itself, e.g.
        `local x = 1; x = x;` then nothing happens. This is an optimization.
     */
    case .Local:
        // If `e` is currently a non-local register, reuse its register
        compiler_expr_pop(c, expr^)
        expr_to_reg(c, expr, var.reg)

    /*
    -   `var.index` is already set to the identifier.
    -   `e` can only be pushed to a register (constants) or reuse an
        existing register (locals, temporaries).
     */
    case .Global:
        reg := compiler_expr_any_reg(c, expr)
        compiler_code(c, .Set_Global, reg = reg, index = var.index)

    /*
    -   `var.table` already refers to the target table and key.
    -   `e` can be emitted to an RK register as is defined in `opcode.odin`.
    -   This allows us to optimize by not needing to push constants to the stack
        beforehand.
     */
    case .Table_Index:
        table := var.table
        rkc   := compiler_expr_rk(c, expr)
        compiler_code(c, .Set_Table, a = table.reg, b = table.key_reg, c = rkc)
    case:
        unreachable("Invalid variable kind %v", var.type)
    }
    // If `e` ended up as a non-local register, reuse its register
    compiler_expr_pop(c, expr^)
}

compiler_set_one_return :: proc(c: ^Compiler, call: ^Expr) {
    // Expression is an open function call?
    if call.type == .Call {
        ip := get_ip(c, call)
        ip.c = 1
        expr_set(call, .Discharged, reg = ip.a)
    }
}

compiler_set_returns :: proc(c: ^Compiler, call: ^Expr, n: int) {
    if call.type == .Call {
        ip := get_ip(c, call)
        ip.c = u16(n)
    }
}

///=== }}} =====================================================================

///=== JUMP MANIPULATION =================================================== {{{


/*
**Analogous to**
-   `lcode.c:condjump(FuncState *fs, OpCode op, int A, int B, int C)` in
    Lua 5.1.5.
*/
compiler_code_cond_jump :: proc(cl: ^Compiler, op: OpCode, a, b, c: u16) -> (jump_pc: int) {
    compiler_code(cl, op, a, b, c)
    return compiler_code_jump(cl)
}


/*
**Analogous to**
-   `compiler.c:emitJump(uint8_t instruction)` in Crafting Interpreters,
    Chapter 23.1: *If Statements*.
-   `lcode.c:luaK_jump(FuncState *fs)` in Lua 5.1.5.

**Returns**
-   The index of our jump instruction in the current chunk's code.
*/
compiler_code_jump :: proc(c: ^Compiler) -> (pc: int) {
    return compiler_code(c, .Jump, reg = 0, jump = NO_JUMP)
}


/*
**Overview**
-   Emit a `.Test_Set` and `.Jump` instruction pair.
-   When `cond` if truthy, we will skip over `.Jump` and continue execution
    as normal.
-   Otherwise, we will not skip `.Jump` and go to wherever it leads us.

**Analogous to**
-   `lcode.c:luaK_goif{true,false}(FuncState *fs, expdesc *e)` in Lua 5.1.5.

**Notes** (2025-05-17):
-   We are always adding new jumps on top of `e.patch_*`.
-   You don't want to use the pc of the newly emitted `.Jump` instruction,
    because for nested logicals `e.patch_*` will always refer to the root
    of the jump list.
 */
compiler_code_go_if :: proc(c: ^Compiler, e: ^Expr, cond: bool) {
    // If we cannot fold or `e` is not already a jump (comparison), we will
    // emit `.Test_Set` with its corresponding jump.
    prev_jump :: proc(c: ^Compiler, e: ^Expr, cond: bool) -> (pc: int) {
        compiler_discharge_vars(c, e)
        #partial switch e.type {
        case .True, .Number, .Constant:
            if cond {
                return NO_JUMP
            }
        case .Nil, .False:
            if !cond {
                return NO_JUMP
            }
        case .Jump:
            if cond {
                invert_comparison(c, e^)
            }
            return e.pc
        }
        // `lcode.c:jumponcond(FuncState *fs, expdesc *e, int (bool) cond)`
        if e.type == .Need_Register {
            if ip := get_ip(c, e); ip.op == .Not {
                // remove previous `not`
                c.pc -= 1
                return compiler_code_cond_jump(c, .Test_Set, ip.b, 0, u16(cond))
            }
            // otherwise, go through
        }
        // Can reuse `e`'s register
        discharge_any_reg(c, e)
        compiler_expr_pop(c, e^)
        return compiler_code_cond_jump(c, .Test_Set, NO_REG, e.reg, u16(!cond))
    }

    get_targets :: proc(e: ^Expr, cond: bool) -> (jump_list, patch_list: ^int) {
        // .And
        if cond {
            return &e.patch_false, &e.patch_true
        }
        // .Or
        return &e.patch_true, &e.patch_false
    }

    pc := prev_jump(c, e, cond)
    jump_list, patch_list := get_targets(e, cond)
    compiler_add_jump(c, jump_list, pc)

    // Only occurs in nested logicals, e.g. `x and y or z`.
    if patch_list^ != NO_JUMP {
        // `NO_REG` is necessary if we want to convert a `.Test_Set` to mere
        // `.Test` by this point.
        reg := NO_REG if pc == NO_JUMP else u16(c.free_reg)
        compiler_patch_jump(c, pc = patch_list^, reg = reg)
        patch_list^ = NO_JUMP
    }
}


/*
**Overview**
-   Patch all elements in the jump list pointed to by `pc` to jump to the
    current free instruction.

**Analogous to**
-   `compiler.c:patchJump(int offset)` in Crafting Interpreters, Chapter 23.1:
    *If Statements*.
-   `lcode.c:patchlistaux(FuncState *fs, int list, int vtarget, int reg,
    int dtarget)` in Lua 5.1.5.
*/
compiler_patch_jump :: proc(c: ^Compiler, pc: int, target: int = NO_JUMP, reg: u16 = NO_REG) {
    /*
    **Analogous to**
    -   `lcode.c:patchetestreg(FuncState *fs, int pc, int reg)` in Lua 5.1.5.
     */
    patch_test_reg :: proc(c: ^Compiler, pc: int, reg: u16) {
        ip := get_jump_control(c, pc)
        if ip.op != .Test_Set {
            return // Cannot be patched
        }
        if reg != NO_REG && reg != ip.b {
            /*
            **Notes** (2025-05-17):
            -   Some register was provided and the register of the expression,
                Reg(B), is not the same as the destination, Reg(A).
            -   Assigns logicals to temporaries, locals, or table fields.
             */
            ip.a = reg
        } else {
            /*
            **Notes** (2025-05-17)
            -   We don't need `.Test_Set` anymore because either `NO_REG` was
                provided or we're assigning to the same register.
            -   This also occurs for setting globals.
             */
            ip^ = ip_make(.Test, ip.b, 0, ip.c)
        }
    }

    // Instead of having a separate `dtarget` (default target) parameter, we
    // use `NO_JUMP` to signal an optional argument.
    // `lcode.c:exp2reg(): fj = luaK_getlabel(fs);`
    target := compiler_get_label(c) if target == NO_JUMP else target
    for pc := pc; pc != NO_JUMP; {
        next := get_jump(c, pc)
        patch_test_reg(c, pc, reg)
        set_jump(c, pc, target)
        pc = next
    }
}


/*
**Overview**
-   Reads the `sBx` argument of the `.Jump` instruction pointed to by `pc` and
    converts it to an absolute pc.

**Analogous to**
-   `lcode.c:getjump(FuncState *fs, int list)` in Lua 5.1.5.
 */
@(private="file")
get_jump :: proc(c: ^Compiler, pc: int) -> (dst: int) {
    assert(pc >= 0)
    ip := get_ip(c, pc)
    // Start of jump list?
    offset := ip_get_sBx(ip^)
    if offset == NO_JUMP {
        return NO_JUMP
    }
    // Turn relative offset into absolute position.
    return (pc + 1) + offset
}


@(private="file")
get_jump_control :: proc(c: ^Compiler, pc: int) -> (ip: ^Instruction) {
    ip = get_ip(c, pc)
    // Have something before the jump instruction?
    if pc >= 1 {
        prev := ptr_offset(ip, -1)
        if opcode_info[prev.op].is_test {
            return prev
        }
    }
    return ip
}


/*
**Analogous to**
-   `lcode.c:invertjump(FuncState *fs, expdesc *e)`
 */
@(private="file")
invert_comparison :: proc(c: ^Compiler, e: Expr) {
    ip := get_jump_control(c, e.pc)
    assert(opcode_info[ip.op].is_test)
    assert(ip.op != .Test && ip.op != .Test_Set)
    ip.a = u16(!bool(ip.a))
}


/*
**Overview**
-   Appends a jump pc, `branch`, to the jump list `list`.
-   Usually `branch` is the address of an `Expr::patch_{true,false}`. This
    is how they get initalized.

**Analogous to**
-   `lcode.c:luaK_concat(FuncState *fs, int *l1, int l2)`

**Notes** (2025-05-17)
-   E.g. given jump list `[.Jump: pc=2, offset=-1]` and branch `.Jump: pc=2`
-   Result: `[.Jump: pc=2, offset=2, .Jump: pc=5, offset=-1]`
*/
compiler_add_jump :: proc(c: ^Compiler, list: ^int, branch: int) {
    if branch == NO_JUMP {
        return
    } else if list^ == NO_JUMP {
        // First jump in the list
        list^ = branch
    } else {
        // pc of first unchained jump in this list.
        pc := list^
        for {
            next := get_jump(c, pc)
            if next == NO_JUMP {
                break
            }
            pc = next
        }
        set_jump(c, pc, branch)
    }
}


/*
**Brief**
-   Redirect the jump at `pc` to do a relative jump to the absolute `target`.

**Analogous to**
-   `lcode.c:fixjump(FuncState *fs, int pc, int dest)` in Lua 5.1.5.
 */
@(private="file")
set_jump :: proc(c: ^Compiler, pc, target: int) {
    ip     := get_ip(c, pc)
    offset := target - (pc + 1) // Absolute pc to relative jump
    assert(target != NO_JUMP)
    if abs(offset) > MAX_sBx {
        parser_error(c.parser, "Jump too large")
    }
    ip_set_sBx(ip, offset)
}


///=== }}} =====================================================================
