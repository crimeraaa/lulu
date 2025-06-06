#+private file
package lulu

import "core:fmt"
import "core:container/small_array"

@(private="package")
Parser :: struct {
    vm:                 ^VM,
    lexer:               Lexer,
    consumed, lookahead: Token,
    recurse:             int,
    block:              ^Block,
}

Block :: struct {
    prev:        ^Block,
    n_outer:      int, // Number of local variables external to this scope.
    break_list:   int,
    is_breakable: bool,
}

Rule :: struct {
    prefix: proc(p: ^Parser, c: ^Compiler) -> Expr,
    infix:  proc(p: ^Parser, c: ^Compiler, left: ^Expr),
    prec:   Precedence,
}


/*
**Links**
-   https://www.lua.org/pil/3.5.html
-   https://www.lua.org/manual/5.1/manual.html#2.5.6
 */
Precedence :: enum {
    None,
    Or,         // or
    And,        // and
    Equality,   // == ~=
    Comparison, // < > <= >=
    Concat,     // ..
    Terminal,   // + -
    Factor,     // * / %
    Unary,      // - # not
    Exponent,   // ^
    Call,       // . ()
    Primary,
}


/*
**Analogous to**
-   `compiler.c:advance()` in Crafting Interpreters, Chapter 17.2: *Parsing
    Tokens*.
 */
@(private="package")
parser_advance :: proc(p: ^Parser) {
    token := lexer_scan_token(&p.lexer)
    p.consumed, p.lookahead = p.lookahead, token
}


/*
**Assumptions**
-   `prev` is the token right before `p.consumed`.
-   Is not called multiple times in a row.

**Guarantees**
-   `p.lexer` is ready to re-consume the current `p.lookahead.lexeme`.
 */
parser_backtrack :: proc(p: ^Parser, prev: Token) {
    p.lexer.current -= len(p.lookahead.lexeme)
    p.consumed, p.lookahead = prev, p.consumed

}


/*
**Analogous to**
-   `compiler.c:consume()` in Crafting Interpreters, Chapter 17.2.1:
    *Handling syntax errors*.
 */
@(private="package")
parser_consume :: proc(p: ^Parser, expected: Token_Type) {
    if p.lookahead.type == expected {
        parser_advance(p)
        return
    }
    // WARNING(2025-01-05): We assume this is enough!
    buf: [64]byte
    s := fmt.bprintf(buf[:], "Expected '%s'", token_type_strings[expected])
    parser_error_lookahead(p, s)
}

@(private="package")
parser_match :: proc(p: ^Parser, expected: Token_Type) -> (found: bool) {
    if found = p.lookahead.type == expected; found {
        parser_advance(p)
    }
    return found
}

parser_check :: proc(p: ^Parser, expected: Token_Type) -> (found: bool) {
    return p.lookahead.type == expected
}

@(private="package")
parser_program :: proc(vm: ^VM, source, input: string) -> ^Function {
    p := &Parser{vm = vm, lexer = lexer_create(vm, input, source)}
    c := &Compiler{}
    f := function_new(vm, source)

    // Main function is always a vararg
    f.chunk.is_vararg = true
    compiler_init(vm, c, p, &f.chunk)
    // Must occur before `block_end()`.
    defer compiler_end(c)

    parser_advance(p)

    // Ensure topmost block is non-nil when resolving locals
    b := block_make(p, c)
    block_set(p, c, &b)

    for !parser_match(p, .Eof) {
        declaration(p, c)
    }

    parser_consume(p, .Eof)
    return f
}

parser_ident :: proc(p: ^Parser) -> ^OString {
    parser_consume(p, .Identifier)
    return ostring_new(p.vm, p.consumed.lexeme)
}

Assign :: struct {
    prev:    ^Assign,
    variable: Expr,
}

/*
**Analogous to**
-   `compiler.c:declaration()` and `compiler.c:statement()` in Crafting
    Interpreters, Chapter 21.1: "Statements".
-   `lparser.c:chunk(LexState *ls)` and `lparser.c:statement(LexState *ls)`
    in Lua 5.1.5.

**Links**
-   https://www.lua.org/source/5.1/lparser.c.html#chunk
-   https://www.lua.org/source/5.1/lparser.c.html#statement
 */
declaration :: proc(p: ^Parser, c: ^Compiler) {
    parser_advance(p)
    #partial switch p.consumed.type {
    case .Identifier:
        // Inline implementation of `compiler.c:parseVariable()` since we
        // immediately consumed the 'identifier'. Also, Lua doesn't have a
        // 'var' keyword.
        last := &Assign{variable = variable(p, c)}
        if parser_match(p, .Left_Paren) {
            function_call(p , c, &last.variable)
            // No returns are used
            ip := get_ip(c, &last.variable)
            ip.c = 0
            c.free_reg -= 1
        } else {
            assignment(p, c, last, 1)
        }
    case .Break:    break_stmt(p, c)
    case .Do:       do_block(p, c)
    case .For:      for_loop(p, c)
    case .Function:
        function_decl(p, c, is_local = false)
    case .If:       if_block(p, c)
    case .Local:
        if parser_match(p, .Function) {
            function_decl(p, c, is_local = true)
        } else {
            local_stmt(p, c)
        }
    case .Print:    print_stmt(p, c)
    case .Return:   return_stmt(p, c)
    case .While:    while_loop(p, c)
    case:
        error_at(p, p.consumed, "Expected an expression")
    }
    // Optional
    parser_match(p, .Semicolon)
}

/*
**Analogous to**
-   `compiler.c:varDeclaration()` and `compiler.c:parserVariable()` (somewhat)
    in Crafting Interpreters, Chapter 21.2: *Variable Declarations*.

**Links**
-   https://www.lua.org/source/5.1/lparser.c.html#exprstat
-   https://www.lua.org/source/5.1/lparser.c.html#prefixexp
-   https://www.lua.org/source/5.1/lparser.c.html#singlevar
 */
assignment :: proc(p: ^Parser, c: ^Compiler, last: ^Assign, n_vars: int) {
    // Don't call `variable()` for the first assignment because we did so already
    // to check for function calls.
    if n_vars > 1 {
        last.variable = variable(p, c)
    }

    // Use recursive calls to create a stack-allocated linked list.
    if parser_match(p, .Comma) {
        parser_consume(p, .Identifier)
        check_recurse(p)
        assignment(p, c, &Assign{prev = last}, n_vars + 1)
        return
    }
    parser_consume(p, .Equals)

    /*
    **Notes** (2025-04-18):
    -   We don't want to immediately push `expr`. This is an optimization
        mainly for the last occurence of `Expr_Type.Table_Index`.
    -   We want to handle each recursive call's associated expression.
     */
    top, n_exprs := expr_list(p, c)
    iter: ^Assign
    if n_exprs == n_vars {
        // last expression can have variadic returns
        compiler_set_1_return(c, &top)
        compiler_store_var(c, &last.variable, &top)

        // `last` is already properly assigned, so skip it
        iter = last.prev
    } else {
        adjust_assign(c, n_vars, n_exprs, &top)
        iter = last
    }

    // Assign from right-to-left, using each topmost register as the assigning
    // value and popping it if possible.
    for target in assign_list(&iter) {
        e := expr_make(.Discharged, reg = cast(u16)c.free_reg - 1)
        compiler_store_var(c, &target.variable, &e)
    }
}

assign_list :: proc(iter: ^^Assign) -> (a: ^Assign, ok: bool) {
    // Current iteration.
    a = iter^

    // Have we exhausted the iterator?
    ok = (a != nil)

    // Prepare for next iteration, but only if we haven't exhausted the
    // iterator.
    if ok {
        iter^ = a.prev
    }
    return a, ok
}


/*
**Form**
-   local_stmt ::= 'local' local_decl [ '=' expr_list ]

**Notes**
-   Due to the differences in Lua and Lox, we cannot combine local variable
    declarations into our `parseVariable()` analog as we do not have a
    catch-all `var` keyword.
*/
local_stmt :: proc(p: ^Parser, c: ^Compiler) {
    n_vars: int
    for {
        s := parser_ident(p)
        local_decl(p, c, s, &n_vars)
        parser_match(p, .Comma) or_break
    }

    top: Expr
    n_exprs: int
    if parser_match(p, .Equals) {
        top, n_exprs = expr_list(p, c)
    }

    /*
    Normally, we want to ensure zero stack effect. However, in this case,
    we want the initialization expressions to remain on the stack as they
    will act as the local variables themselves.
    */
    adjust_assign(c, n_vars, n_exprs, &top)
    local_adjust(c, n_vars)
}


/*
**Form**
-   local_decl ::= identifier [',' identifier]*

**Analogous to**
-   `compiler.c:declareVariable()` in Crafting Interpreters, Chapter 22.3:
    *Declaring Local Variables*.
-   `lparser.c:new_localvar(LexState *ls, TString *name, int n)` in Lua 5.1.5.
 */
local_decl :: proc(p: ^Parser, c: ^Compiler, ident: ^OString, n: ^int) {
    locals := c.chunk.locals
    #reverse for active in small_array.slice(&c.active)[p.block.n_outer:] {
        local := locals[active]
        if local.ident == ident {
            parser_error(p, "Shadowing of local variable")
        }
    }

    /*
    **Notes (2025-04-18)**
    -   In `new_localvar()`, Lua DOES push to the `ls->actvar[]` array but does
        not increment `ls->nactvar`.
    -   This is likely how they keep track of "uninitialized" locals.
    -   So we don't want to push because that will mutate the length thus
        messing up our "uninitialized" counter.
     */
    active_reg := small_array.len(c.active) + n^
    if active_reg >= MAX_LOCALS {
        buf: [64]byte
        msg := fmt.bprintf(buf[:], "More than %i local variables", MAX_LOCALS)
        parser_error(p, msg)
    }
    local_index := compiler_add_local(c, ident)
    small_array.set(&c.active, index = active_reg, item = local_index)
    n^ += 1
}


/*
**Notes**
-   See `lparser.c:adjust_assign(LexState *ls, int nvars, int nexps, expdesc *e)`.
 */
adjust_assign :: proc(c: ^Compiler, n_vars, n_exprs: int, expr: ^Expr) {
    extra := n_vars - n_exprs

    // `lcode.c:luaK_setreturns(FuncState *fs, expdesc *e, int nresults)`
    if expr.type == .Call {
        // Include function object itself
        extra += 1
        ip := get_ip(c, expr)
        // Stack slot of caller object will be overridden
        ip.c = u16(extra)
        if extra > 1 {
            compiler_reg_reserve(c, extra - 1)
        }
        return
    }

    // Push the last expression from `expr_list()`.
    if expr.type != .Empty {
        compiler_expr_next_reg(c, expr)
    }

    // More variables than expressions?
    if extra > 0 {
        reg := c.free_reg
        compiler_reg_reserve(c, extra)
        compiler_code_nil(c, cast(u16)reg, cast(u16)extra)
    } else {
        /*
        **Sample**
        -   local a, b, c = 1, 2, 3, 4

        **Results**
        -   free_reg = 4
        -   n_vars   = 3
        -   n_exprs  = 4

        **Assumptions**
        -   If `n_exprs == n_vars`, nothing changes as we subtract 0.
         */
        c.free_reg -= n_exprs - n_vars
    }
}


/*
**Analogous to**
-   `lparser.c:adjustlocalvars(LexState *ls, int nvars)` in Lua 5.1.5.

**Notes**
-   We don't need a `remove_locals()` function because `compiler_end_scope()`
    takes care of that already.
 */
local_adjust :: proc(c: ^Compiler, n_vars: int) {
    startpc := c.pc

    /*
    **Assumptions**
    -   This relies on the next `n_vars` elements in the array having been set
        previously by `local_decl`.
    -   `c.active.len` was NOT incremented yet.
     */
    n_active := small_array.len(c.active) + n_vars
    small_array.resize(&c.active, n_active)
    active := small_array.slice(&c.active)
    locals := c.chunk.locals
    for i := n_vars; i > 0; i -= 1 {
        // `lparser.c:getlocvar(fs, i)`
        index := active[n_active - i]
        local := &locals[index]
        local.startpc = startpc
    }
}

do_block :: proc(p: ^Parser, c: ^Compiler) {
    b := block_make(p, c)
    block_set(p, c, &b)
    for !still_in_block(p) {
        declaration(p, c)
    }
    parser_consume(p, .End)
}

block_make :: proc(p: ^Parser, c: ^Compiler, is_loop := false) -> Block {
    return Block{
        prev         = p.block,
        n_outer      = small_array.len(c.active),
        break_list   = NO_JUMP,
        is_breakable = is_loop,
    }
}

@(deferred_in=block_end)
block_set :: proc(p: ^Parser, c: ^Compiler, b: ^Block) {
    p.block = b
}

/*
**Analogous to**
-   `lparser.c:removevars(LexState *ls, int tolevel)` in Lua 5.1.5.
 */
block_end :: proc(p: ^Parser, c: ^Compiler, b: ^Block) {
    defer p.block = b.prev

    active := small_array.slice(&c.active)
    locals := c.chunk.locals[:c.count.locals]
    limit  := b.n_outer
    endpc  := c.pc

    // Don't pop registers as we'll go below the active count!
    for reg := small_array.len(c.active) - 1; reg >= limit; reg -= 1 {
        index := active[reg]
        local := &locals[index]
        small_array.pop_back(&c.active)
        local.endpc = endpc
    }
    c.free_reg = limit
}

/*
**Analogous to**
-   `lparser.c:block_follow(LexState *ls)` in Lua 5.1.5
 */
still_in_block :: proc(p: ^Parser) -> bool {
    #partial switch p.lookahead.type {
    case .Else, .Elseif, .End, .Until, .Eof: return true
    }
    return false
}


/*
**Form**
```
if_block ::= 'if' expression 'then' block 'end'
```

**Analogous to**
-   `compiler.c:ifStatement()` in Crafting Interpreters, Chapter 23.1:
    *If Statements*.
*/
if_block :: proc(p: ^Parser, c: ^Compiler) {
    then_cond :: proc(p: ^Parser, c: ^Compiler) -> (cond: Expr) {
        cond = expression(p, c)
        parser_consume(p, .Then)
        compiler_code_go_if(c, &cond, true)
        then_block(p, c)
        return cond
    }

    then_block :: proc(p: ^Parser, c: ^Compiler) {
        check_recurse(p)
        b := block_make(p, c)
        block_set(p, c, &b)
        for !still_in_block(p) {
            declaration(p, c)
        }
    }

    then_expr := then_cond(p, c)
    else_jump := NO_JUMP // No unconditional jump over `else` by default.
    for parser_match(p, .Elseif) {
        // all child non-`else` branches skip over the one `else` branch
        compiler_add_jump(c, &else_jump, compiler_code_jump(c))
        compiler_patch_jump(c, then_expr.patch_false)

        // each `then` jump is independent of the next; they are tried in order
        then_expr = then_cond(p, c)
    }

    if parser_match(p, .Else) {
        compiler_add_jump(c, &else_jump, compiler_code_jump(c))
        compiler_patch_jump(c, then_expr.patch_false)
        then_block(p, c)
    } else {
        compiler_patch_jump(c, then_expr.patch_false)
    }
    compiler_patch_jump(c, else_jump)
    parser_consume(p, .End)
}


function_decl :: proc(p: ^Parser, c: ^Compiler, is_local: bool) {
    var: Expr
    if is_local {
        n: int
        ident := parser_ident(p)
        local_decl(p, c, ident, &n)
        local_adjust(c, n)
        var = expr_make(.Local, reg = u16(c.free_reg))
    } else {
        parser_consume(p, .Identifier)
        var = variable(p, c)
    }
    fexpr := function_body(p, c)
    compiler_store_var(c, &var, &fexpr)
}

function_body :: proc(p: ^Parser, c: ^Compiler) -> Expr {
    parser_consume(p, .Left_Paren)
    f  := function_new(p.vm, p.lexer.source)
    c2 := &Compiler{parent = c}

    compiler_init(p.vm, c2, p, &f.chunk)
    // Must occur before `block_end()`.
    defer compiler_end(c2)

    // Ensure topmost block is non-nil when resolving locals
    b := block_make(p, c2)
    block_set(p, c2, &b)

    for !parser_check(p, .Right_Paren) {
        if parser_match(p, .Ellipsis_3) {
            f.chunk.is_vararg = true
            break
        }
        param := parser_ident(p)
        local_decl(p, c2, param, &f.arity)
        parser_match(p, .Comma) or_break
    }
    parser_consume(p, .Right_Paren)
    local_adjust(c2, f.arity)
    compiler_reg_reserve(c2, f.arity)
    do_block(p, c2)
    i := compiler_add_constant(c2.parent, f)
    return expr_make(.Constant, index = i)
}


/*
**Assumptions**
-   `call` was the result of `variable()`.
 */
function_call :: proc(p: ^Parser, c: ^Compiler, call: ^Expr) {
    // Function to be called must be on the stack.
    compiler_expr_next_reg(c, call)

    args: Expr
    n_args: int
    if !parser_match(p, .Right_Paren) {
        args, n_args = expr_list(p, c)
        // Push the last expression from `expr_list()`.
        compiler_expr_next_reg(c, &args)
        parser_consume(p, .Right_Paren)
    }

    // Assume 1 return by default.
    call_pc := compiler_code(c, .Call, a = call.reg, b = u16(n_args), c = 1)
    call^ = expr_make(.Call, pc = call_pc)
    // Don't pop the function object (just yet)
    c.free_reg -= n_args
}

vararg :: proc(p: ^Parser, c: ^Compiler) -> Expr {
    if !c.chunk.is_vararg {
        parser_error(p, "Function does not have varargs")
    }
    parser_error(p, "Varargs not yet supported")
}


/*
**Notes**
-   See `lparser.c:funcargs(LexState *ls, expdesc *e)`.
*/
print_stmt :: proc(p: ^Parser, c: ^Compiler) {
    c.is_print = true
    defer c.is_print = false

    parser_consume(p, .Left_Paren)

    args: Expr
    n_args: int
    base_reg := u16(c.free_reg)
    if !parser_match(p, .Right_Paren) {
        args, n_args = expr_list(p, c)
        // Push the last expression from `expr_list()`.
        compiler_expr_next_reg(c, &args)
        parser_consume(p, .Right_Paren)
    }

    last_reg := u16(c.free_reg) // If > MAX_A should still fit
    compiler_code(c, .Print, ra = base_reg, rb = last_reg)
    c.free_reg -= n_args
}

while_loop :: proc(p: ^Parser, c: ^Compiler) {
    loop_start := c.pc
    cond := expression(p, c)
    parser_consume(p, .Do)

    b := block_make(p, c, is_loop = true)
    block_set(p, c, &b)
    compiler_code_go_if(c, &cond, true)
    compiler_add_jump(c, &b.break_list, cond.patch_false)
    do_block(p, c)
    compiler_patch_jump(c, compiler_code_jump(c), target = loop_start)
    compiler_patch_jump(c, b.break_list)
}

for_loop :: proc(p: ^Parser, c: ^Compiler) {
    local_literal :: proc(p: ^Parser, c: ^Compiler, s: string, n_vars: ^int) {
        o := ostring_new(p.vm, s)
        local_decl(p, c, o, n_vars)
    }

    ident := parser_ident(p)
    base := c.free_reg
    b := block_make(p, c, is_loop = true)
    block_set(p, c, &b)

    parser_consume(p, .Equals)
    init := expression(p, c) // for index initial value
    compiler_expr_next_reg(c, &init)

    parser_consume(p, .Comma)
    cond := expression(p, c) // for condition
    compiler_expr_next_reg(c, &cond)

    incr := expression(p, c) if parser_match(p, .Comma) else expr_make(Number(1))
    compiler_expr_next_reg(c, &incr)
    parser_consume(p, .Do)

    n_vars: int
    local_decl(p, c, ident, &n_vars) // for index
    local_literal(p, c, "(for cond)", &n_vars)
    local_literal(p, c, "(for incr)", &n_vars)
    local_adjust(c, n_vars)

    prep_pc := compiler_code(c, .For_Prep, u16(base), NO_JUMP)
    do_block(p, c)

    loop_pc := compiler_code(c, .For_Loop, u16(base), NO_JUMP)
    compiler_patch_jump(c, prep_pc, target = loop_pc)
    compiler_patch_jump(c, loop_pc, target = prep_pc + 1)
    compiler_patch_jump(c, b.break_list)
}

break_stmt :: proc(p: ^Parser, c: ^Compiler) {
    b := p.block
    // Get potentially-breakable parents of non-breakable blocks
    // e.g. `if`, `elseif`, `else`
    for b != nil && !b.is_breakable {
        b = b.prev
    }
    if b == nil {
        parser_error(p, "No loop to break")
    }
    compiler_add_jump(c, &b.break_list, compiler_code_jump(c))
}

return_stmt :: proc(p: ^Parser, c: ^Compiler) {
    if !still_in_block(p) && !parser_check(p, .Semicolon) {
        base := u16(c.free_reg)
        top, count := expr_list(p, c)
        compiler_expr_next_reg(c, &top)
        compiler_code_return(c, reg = base, count = u16(count))
    } else {
        // Don't advance; if in main block we'll check for EOF after
        // Can't assume we can safely index `c.free_reg`.
        compiler_code_return(c, reg = 0, count = 0)
    }
}


/*
**Form**
-   expr_list ::= expression [',' expression]*

**Overview**
-   Pushes a comma-separated list of expressions onto the stack, save for the
    last expression.

**Notes**
-   Like in Lua 5.1.5, the last expression is not emitted.
 */
expr_list :: proc(p: ^Parser, c: ^Compiler) -> (top: Expr, count: int) {
    count = 1 // at least one expression
    top   = expression(p, c)
    for parser_match(p, .Comma) {
        compiler_expr_next_reg(c, &top)
        top = expression(p, c)
        count += 1
    }
    return top, count
}


/*
**Form**
-   expression ::= literal | unary | grouping | arith | compare | variable

**Analogous to**
-   `compiler.c:expression()` in Crafting Interpreters, Chapter 17.4:
    *Parsing Prefix Expressions*.

**Notes**
-   Expressions only ever produce 1 net resulting value, which should reside in `expr`.
-   However, `expr` itself does not reside in a register yet. It is up to you
    to decide how to allocate that.
 */
expression :: proc(p: ^Parser, c: ^Compiler) -> (expr: Expr) {
    return parse_precedence(p, c, .None + Precedence(1))
}


/*
**Analogous to**
-   `lparser.c:subexpr(LexState *ls, expdesc *v, int limit)` in Lua 5.1.5.

**Links**
-   https://www.lua.org/source/5.1/lparser.c.html#subexpr
 */
parse_precedence :: proc(p: ^Parser, c: ^Compiler, prec: Precedence) -> Expr {
    check_recurse(p)

    parser_advance(p)
    prefix := get_rule(p.consumed.type).prefix
    if prefix == nil {
        parser_error(p, "Expected an expression")
    }

    // Prefix expressions are always the "root" node. We don't know if we're in
    // a recursive call.
    left := prefix(p, c)
    for {
        rule := get_rule(p.lookahead.type)
        if prec > rule.prec {
            break
        }
        // Can occur when we hardcode low precedence recursion in high precedence calls
        assert(rule.infix != nil)
        parser_advance(p)

        // Infix expressions are the actual branches.
        rule.infix(p, c, &left)
    }
    return left
}


/*
**Analogous to**
-   `compiler.c:errorAtCurrent()` in Crafting Interpreters, Chapter 17.2.1:
    *Handling syntax errors*.
 */
parser_error_lookahead :: proc(p: ^Parser, msg: string) -> ! {
    error_at(p, p.lookahead, msg)
}


/*
**Analogous to**
-   `compiler.c:error()` in Crafting Interpreters, Chapter 17.2.1: *Handling
    syntax errors*.
 */
@(private="package")
parser_error :: proc(p: ^Parser, msg: string) -> ! {
    error_at(p, p.consumed, msg)
}


/*
**Analogous to**
-   `compiler.c:errorAt()` in Crafting Interpreters, Chapter 17.2.1: *Handling
    syntax errors*.
 */
error_at :: proc(p: ^Parser, token: Token, msg: string) -> ! {
    source := p.lexer.source
    line   := token.line
    // .Eof token: don't use lexeme as it'll just be an empty string.
    loc := token.lexeme if token.type != .Eof else token_type_strings[.Eof]
    vm_syntax_error(p.vm, source, line, "%s at '%s'", msg, loc)
}


///=== PREFIX EXPRESSIONS ================================================== {{{


/*
**Form**
-   grouping ::= '(' expression ')'
 */
grouping :: proc(p: ^Parser, c: ^Compiler) -> Expr {
    expr := expression(p, c)
    parser_consume(p, .Right_Paren)
    return expr
}

@(deferred_in=check_recurse_end)
check_recurse :: proc(p: ^Parser) {
    if p.recurse += 1; p.recurse >= PARSER_MAX_RECURSE {
        parser_error(p, "Too many syntax levels")
    }
}

check_recurse_end :: proc(p: ^Parser) {
    p.recurse -= 1
}


/*
**Form**
-   `literal ::= 'nil' | 'true' | 'false' | NUMBER | STRING`
 */
literal :: proc(p: ^Parser, c: ^Compiler) -> Expr {
    #partial switch p.consumed.type {
    case .Nil:    return expr_make(Expr_Type.Nil)
    case .True:   return expr_make(true)
    case .False:  return expr_make(false)
    case .Number: return expr_make(p.consumed.number)
    case .String:
        index := compiler_add_constant(c, p.consumed.ostring)
        return expr_make(.Constant, index = index)
    case:
        unreachable("Invalid literal token %v", p.consumed.type)
    }
}

/*
**Form**
-   variable ::= identifier [ indexed | ( '.' identifier ) ]*

**Analogous to:**
-   `lparser.c:singlevar(LexState *ls, Expr *var)` and
    `lparser.c:singlevaraux(LexState *ls, TString *n, Expr *var, int base)`
    in Lua 5.1.5.

**Assumptions**
-   `p.consumed` is of type `.Identifier`.
 */
variable :: proc(p: ^Parser, c: ^Compiler) -> Expr {
    /*
    **Analogous to**
    -   `compiler.c:namedVariable(Token name)` in Crafting Interpreters,
        Chapter 21.3: *Reading Variables*.
     */
    first_var :: proc(p: ^Parser, c: ^Compiler) -> Expr {
        ident := ostring_new(p.vm, p.consumed.lexeme)
        if local, ok := compiler_resolve_local(c, ident); ok {
            return expr_make(.Local, reg = local)
        }
        index := compiler_add_constant(c, ident)
        return expr_make(.Global, index = index)
    }

    /*
    **Form**
    -   index ::= '[' expression ']'

    **Overview**
    -   Compiles an expression and saves it to a new `Expr` instance.
    -   This instance represents either a get-operation or a literal.

    **Analogous to**
    -   `lparser.c:yindex(LexState *ls, expdesc *var)` in Lua 5.1.5.
     */
    index :: proc(p: ^Parser, c: ^Compiler) -> (key: Expr) {
        key = expression(p, c)
        compiler_expr_to_value(c, &key)
        parser_consume(p, .Right_Bracket)
        return key
    }

    var := first_var(p, c)
    table_fields: for {
        prev := p.consumed
        parser_advance(p)
        #partial switch p.consumed.type {
        case .Left_Bracket:
            /*
            **Overview**
            -   Emit the parent table of this index.

            **Notes** (2025-04-18):
            -   If it's a local then just reuse the register.
            -   If it's a constant then try to use RK, else push it.
             */
            compiler_expr_any_reg(c, &var)
            key := index(p, c)
            compiler_code_indexed(c, &var, &key)
        case .Period:
            // Same idea as in `.Left_Bracket` case.
            compiler_expr_any_reg(c, &var)
            key := field_name(p, c)
            compiler_code_indexed(c, &var, &key)
        case .Colon:
            parser_error(p, "':' syntax not yet supported")
        case:
            parser_backtrack(p, prev)
            break table_fields
        }
    }
    return var
}


/*
**Form**
-   field_name ::= identifier

**Analogous to**
-   `compiler.c:identifierConstant(Token *name)` in Crafting Interpreters,
    Chapter 21.2, *Variable Declarations*.

**Overview**
-   Save fieldname in an expression which we can emit as an RK.

**Assumptions**
-   The desired field name (an `.Identifier`) was just consumed.

**Notes**
-   If the index does not fit in an RK, you will have to push it yourself!
 */
field_name :: proc(p: ^Parser, c: ^Compiler) -> (key: Expr) {
    o := parser_ident(p)
    return expr_make(.Constant, index = compiler_add_constant(c, o))
}


/*
**Form**
-   table ::= '{' table_element? [ ',' table_element ]* '}'
    table_element ::= expression
                    | ( indexed | identifier) '=' expression


**Assumptions**
-   The left curly brace token was just consumed.
 */
constructor :: proc(p: ^Parser, c: ^Compiler) -> Expr {
    /*
    **Analogous to**
    -   `lparser.c:ConsControl` in Lua 5.1.5.
     */
    Constructor :: struct {
        table:           Expr, // table descriptor
        n_array, n_hash: int,
        to_store:        int, // number of array elements pending to be stored
    }


    /*
    **Analogous to**
    -   `lparser.c:closelistfield(LexState *ls, struct ConsControl *cc)` in
        Lua 5.1.5.
     */
    array :: proc(p: ^Parser, c: ^Compiler, ctor: ^Constructor) {
        defer {
            ctor.n_array += 1
            ctor.to_store    += 1
        }

        value := expression(p, c)
        compiler_expr_next_reg(c, &value)
    }

    /*
    **Assumptions**
    -   The `.Equals` token was NOT yet consumed, it should still be the
        desired field name.
     */
    field :: proc(p: ^Parser, c: ^Compiler, ctor: ^Constructor) {
        defer ctor.n_hash += 1

        key := field_name(p, c)
        parser_consume(p, .Equals)
        rkb := compiler_expr_rk(c, &key)

        value := expression(p, c)
        rkc   := compiler_expr_rk(c, &value)
        compiler_code(c, .Set_Table, a = ctor.table.reg, b = rkb, c = rkc)
        compiler_expr_pop(c, value)
        compiler_expr_pop(c, key)
    }

    index :: proc(p: ^Parser, c: ^Compiler, ctor: ^Constructor) {
        defer ctor.n_hash += 1

        key := expression(p, c)
        parser_consume(p, .Right_Bracket)
        rkb := compiler_expr_rk(c, &key)

        parser_consume(p, .Equals)

        value := expression(p, c)
        rkc := compiler_expr_rk(c, &value)

        compiler_code(c, .Set_Table, a = ctor.table.reg, b = rkb, c = rkc)
        // Reuse these registers
        compiler_expr_pop(c, value)
        compiler_expr_pop(c, key)
    }

    // All information is pending so just use 0's, we'll fix it later
    pc   := compiler_code(c, .New_Table, a = 0, b = 0, c = 0)
    ctor := &Constructor{table = expr_make(.Need_Register, pc = pc)}
    compiler_expr_next_reg(c, &ctor.table)

    for !parser_match(p, .Right_Curly) {
        /*
        **Analogous to**
        -   `lparser.c:closelistfield(LexState *ls, struct ConsControl *cc)`
         */
        if ctor.to_store == FIELDS_PER_FLUSH {
            compiler_code_set_array(c, ctor.table.reg, ctor.n_array,
                                    ctor.to_store)
            ctor.to_store = 0
        }

        // for backtracking
        prev := p.consumed
        parser_advance(p)
        #partial switch p.consumed.type {
        case .Identifier:
            // `.Equals` is consumed inside of `field`
            if parser_check(p, .Equals) {
                parser_backtrack(p, prev)
                field(p, c, ctor)
            } else {
                parser_backtrack(p, prev)
                array(p, c, ctor)
            }
        case .Left_Bracket:
            index(p, c, ctor)
        case:
            parser_backtrack(p, prev)
            array(p, c, ctor)
        }

        if !parser_match(p, .Comma) {
            parser_consume(p, .Right_Curly)
            break
        }
    }

    /*
    **Analogous to**
    -   `lparser.c:lastlistfield(LexState *ls, struct ConsControl *cc)`

    TODO(2025-04-15): Add the other `if` branches!
     */
    if ctor.to_store != 0 {
        // if (hasmultret(cc->value.kind)) ...
        // if (cc->v.k != VVOID) ...
        compiler_code_set_array(c, ctor.table.reg, ctor.n_array, ctor.to_store)
    }

    // `fb_make()` may also round up the values by some factor, but that's
    // okay because our hash table will simply over-allocate.
    ip := get_ip(c, pc)
    ip.b = cast(u16)fb_make(ctor.n_array)
    ip.c = cast(u16)fb_make(ctor.n_hash)
    return ctor.table
}


/*
**Form**
-   unary ::= unary_op expression
    unary_op ::= '-' | 'not' | '#'

**Assumptions**
-   The desired unary operator was just consumed.

**Guarantees**
-   For arithetic and comparison, `expr` ends up as RK.
-   For len, `expr` ends up as a register.
 */
unary :: proc(p: ^Parser, c: ^Compiler) -> Expr {
    // MUST be set to '.Number' in order to try constant folding.
    @(static)
    dummy := Expr{type = .Number, patch_true = NO_JUMP, patch_false = NO_JUMP}
    type  := p.consumed.type
    arg   := parse_precedence(p, c, .Unary)

    /*
    **Links**
    -   https://www.lua.org/source/5.1/lcode.c.html#luaK_prefix
    -   https://the-ravi-programming-language.readthedocs.io/en/latest/lua-p.html#state-transitions

    **Notes**
    -   Inline implementation of the only relevant lines from `lcode.c:luaK_prefix()`.
    -   Ensure the zero-value for `Expr_Type` is anything BUT `.Discharged`.
    -   Otherwise, calls to `compiler_pop_expr()` will push through and mess up
        the free registers counter.
     */
    #partial switch type {
    case .Dash:
        when USE_CONSTANT_FOLDING {
            // Don't fold non-numeric constants (for arithmetic) or non-falsifiable
            // expressions (for comparison).
            #partial switch arg.type {
            case .Nil, .True, .False, .Number:
                expr_has_jumps(arg) or_break
                fallthrough
            case:
                compiler_expr_any_reg(c, &arg)
            }
        } else {
            // If nested (e.g. `-(-x)`) reuse the register we stored `x` in
            compiler_expr_any_reg(c, &arg)
        }
        compiler_code_arith(c, .Unm, &arg, &dummy)
    case .Not:
        compiler_code_not(c, &arg)
    case .Pound:
        // OpCode.Len CANNOT operate on constants no matter what.
        compiler_expr_any_reg(c, &arg)
        compiler_code_arith(c, .Len, &arg, &dummy)
    case:
        unreachable("Token %v is not an unary operator", type)
    }

    return arg
}

///=== }}} =====================================================================

///=== INFIX EXPRESSIONS =================================================== {{{


/*
**Form**
-   arith    ::= expression arith_op expression
    arith_op ::= '+' | '-' | '*' | '/' | '%' | '^'

**Notes**
-   '..' is not included due to its unique semantics: neither arguments B nor C
    can be RK.
 */
arith :: proc(p: ^Parser, c: ^Compiler, left: ^Expr) {
    arith_op :: proc(type: Token_Type) -> (op: OpCode, prec: Precedence) {
        #partial switch type {
        case .Plus:    op = .Add
        case .Dash:    op = .Sub
        case .Star:    op = .Mul
        case .Slash:   op = .Div
        case .Percent: op = .Mod
        case .Caret:   op = .Pow
        case:
            unreachable("Impossible condition reached; got token %v", type)
        }
        return op, get_rule(type).prec
    }

    when USE_CONSTANT_FOLDING {
        if !expr_is_number(left^) {
            compiler_expr_rk(c, left)
        }
    } else {
        /*
        NOTE(2025-01-19):
        -   This is necessary when both sides are nonconstant binary expressions!
            e.g. `'h' .. 'i' == 'h' .. 'i'`
        -   This is because, by itself, expressions like `concat` result in
            `.Need_Register`.
         */
        compiler_expr_rk(c, left)
    }

    op, prec := arith_op(p.consumed.type)

    // By not adding 1 for exponentiation we enforce right-associativity since we
    // keep emitting the ones to the right first
    if prec != .Exponent {
        prec += Precedence(1)
    }

    right := parse_precedence(p, c, prec)
    compiler_code_arith(c, op, left, &right) // `lcode.c:luaK_posfix()`
}


/*
**Form**
-   compare    ::= compare_op expression
    compare_op ::= '==' | '<' | '<=' | '~=' | '>=' | '>'
*/
compare :: proc(p: ^Parser, c: ^Compiler, left: ^Expr) {
    compare_op :: proc(type: Token_Type) -> (op: OpCode, cond: bool, prec: Precedence) {
        #partial switch type {
        case .Equals_2:       op = .Eq;  cond = true
        case .Left_Angle:     op = .Lt;  cond = true
        case .Left_Angle_Eq:  op = .Leq; cond = true
        case .Tilde_Eq:       op = .Eq;  cond = false
        case .Right_Angle:    op = .Lt;  cond = false
        case .Right_Angle_Eq: op = .Leq; cond = false
        case:
            unreachable("Impossible condition reached; got token %v", type)
        }
        return op, cond, get_rule(type).prec
    }

    // See comment in `compiler.odin:arith()`.
    when USE_CONSTANT_FOLDING {
        #partial switch left.type {
        case .Nil, .True, .False, .Number:
            expr_has_jumps(left^) or_break
            fallthrough
        case:
            compiler_expr_rk(c, left)
        }
    } else {
        compiler_expr_rk(c, left)
    }
    op, cond, prec := compare_op(p.consumed.type)
    right := parse_precedence(p, c, prec)
    compiler_code_compare(c, op, cond, left, &right)
}


/*
**Form**
-   concat ::= expression ['..' expression]+

**Assumptions**
-   `..` was just consumed and the right-hand-side operand is the lookahead.
-   The left-hand-side operand already resides in `left`.

**Guarantees**
-   `left` will be pushed to the stack, then it will be of type `.Need_Register`
    where its `pc` field will refer to the location of `OpCode.Concat`.

**Notes**
-   Concat is treated as right-associative for optimization via recursive calls.
-   This means that recursive call's `left` parameter will also refer to its
    parent caller's `right` parameter.
-   This is because attempting to optimize within a loop is a lot harder than it
    seems.

**Links**
-   http://lua-users.org/wiki/AssociativityOfConcatenation
 */
concat :: proc(p: ^Parser, c: ^Compiler, left: ^Expr) {
    // Left-hand operand MUST be on the stack
    compiler_expr_next_reg(c, left)

    // If recursive concat, this will be `.Need_Register` as well.
    right := parse_precedence(p, c, .Concat)
    compiler_code_concat(c, left, &right)
}


/*
**Visualization**
```
        <left>
    +-- Test_Set R(A) <left> !$COND
    |   ; if bool(<left>) == bool(!$COND) then R(A) := <left> else goto <right>
+---|-- Jump 0 1
|   |   ; goto <right + 1>
|   +-> <right>
|       ; R(A) := <right>
+-----> ...
```
*/
logic :: proc(p: ^Parser, c: ^Compiler, left: ^Expr) {
    logic_op :: proc(type: Token_Type) -> (cond: bool, prec: Precedence) {
        #partial switch type {
        case .And: return true,  .And
        case .Or:  return false, .Or
        case:
            unreachable("Impossible condition reached; got token %v", type)
        }
    }

    cond, prec := logic_op(p.consumed.type)
    compiler_code_go_if(c, left, cond)

    // Treat logical operators as left-associative so we don't needlessly
    // recurse; e.g. `x and y and z` is parsed as `(x and y) and z` rather
    // than `x and (y and z)`.
    right := parse_precedence(p, c, prec + Precedence(1))

    // `luaK_posfix()`- ensure these lists are closed.
    if cond {
        assert(left.patch_true  == NO_JUMP)
    } else {
        assert(left.patch_false == NO_JUMP)
    }

    // Don't change `left.patch_{true,false}`- we need it as-is!
    left.type = right.type
    left.info = right.info
}

///=== }}} =====================================================================


get_rule :: proc(type: Token_Type) -> (rule: Rule) {
    @(static, rodata)
    rules := #partial [Token_Type]Rule {
        // Keywords
        .And        = {infix  = logic, prec = .And},
        .False      = {prefix = literal},
        .Function   = {prefix = function_body},
        .Nil        = {prefix = literal},
        .Not        = {prefix = unary},
        .Or         = {infix  = logic, prec = .Or},
        .True       = {prefix = literal},

        // Balanced Pairs
        .Left_Paren = {prefix = grouping, infix = function_call, prec = .Call},
        .Left_Curly = {prefix = constructor},

        // Arithmetic
        .Plus       = {prefix = nil,   infix = arith, prec = .Terminal},
        .Dash       = {prefix = unary, infix = arith, prec = .Terminal},
        .Star       = {prefix = nil,   infix = arith, prec = .Factor},
        .Slash      = {prefix = nil,   infix = arith, prec = .Factor},
        .Percent    = {prefix = nil,   infix = arith, prec = .Factor},
        .Caret      = {prefix = nil,   infix = arith, prec = .Exponent},

        // Comparison
        .Equals_2       = {infix = compare, prec = .Equality},
        .Tilde_Eq       = {infix = compare, prec = .Equality},
        .Left_Angle     = {infix = compare, prec = .Comparison},
        .Left_Angle_Eq  = {infix = compare, prec = .Comparison},
        .Right_Angle    = {infix = compare, prec = .Comparison},
        .Right_Angle_Eq = {infix = compare, prec = .Comparison},

        // Other
        .Ellipsis_3 = {prefix = vararg},
        .Ellipsis_2 = {infix  = concat, prec = .Concat},
        .Pound      = {prefix = unary},

        // Literals
        .Number     = {prefix = literal},
        .String     = {prefix = literal},
        .Identifier = {prefix = variable},
    }
    return rules[type]
}

