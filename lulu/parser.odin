#+private
package lulu

import "core:fmt"
import sa "core:container/small_array"

Parser :: struct {
    vm:                 ^VM,
    lexer:               Lexer,
    consumed, lookahead: Token,
    recurse:             int,
}

Parse_Rule :: struct {
    prefix: proc(parser: ^Parser, compiler: ^Compiler) -> Expr,
    infix:  proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr),
    prec:   Precedence,
}


/*
**Links**
-   https://www.lua.org/pil/3.5.html
-   https://www.lua.org/manual/5.1/manual.html#2.5.6
 */
Precedence :: enum u8 {
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
-   'compiler.c:advance()' in the book.
 */
parser_advance :: proc(parser: ^Parser) {
    token := lexer_scan_token(&parser.lexer)
    parser.consumed, parser.lookahead = parser.lookahead, token
}


/*
**Assumptions**
-   `previous` is the token right before `parser.consumed`.
-   Is not called multiple times in a row.

**Guarantees**
-   `parser.lexer` points to the start of the lexeme before `parser.lookahead`.
-   When we call `parser_advance()`, we end up back at the old lookahead.
 */
parser_backtrack :: proc(parser: ^Parser, previous: Token) {
    lexer := &parser.lexer
    lexer.start   -= len(parser.lookahead.lexeme)
    lexer.current = lexer.start
    parser.consumed, parser.lookahead = previous, parser.consumed

}


/*
**Analogous to**
-   'compiler.c:consume()' in the book.
 */
parser_consume :: proc(parser: ^Parser, expected: Token_Type) {
    if parser.lookahead.type == expected {
        parser_advance(parser)
        return
    }
    // WARNING(2025-01-05): We assume this is enough!
    buf: [64]byte
    s := fmt.bprintf(buf[:], "Expected '%s'", token_type_strings[expected])
    parser_error_lookahead(parser, s)
}

parser_match :: proc(parser: ^Parser, expected: Token_Type) -> (found: bool) {
    if parser.lookahead.type == expected {
        parser_advance(parser)
        return true
    }
    return false
}

parser_check :: proc(parser: ^Parser, expected: Token_Type) -> (found: bool) {
    return parser.lookahead.type == expected
}

LValue :: struct {
    prev:    ^LValue,
    variable: Expr,
}

/*
**Analogous to**
-   `compiler.c:declaration()` in the book.
-   `lparser.c:chunk(LexState *ls)` in Lua 5.1.5.
-   `compiler.c:statement()` in the book.
-   `lparser.c:statement(LexState *ls)` in Lua 5.1.5.

**Links**
-   https://www.lua.org/source/5.1/lparser.c.html#chunk
-   https://www.lua.org/source/5.1/lparser.c.html#statement
 */
parser_parse :: proc(parser: ^Parser, compiler: ^Compiler) {
    parser_advance(parser)
    #partial switch parser.consumed.type {
    case .Identifier:
        // Inline implementation of `compiler.c:parseVariable()` since we immediately
        // consumed the 'identifier'. Also, Lua doesn't have a 'var' keyword.
        last := &LValue{variable = variable(parser, compiler)}
        if parser_match(parser, .Left_Paren) {
            parser_error_consumed(parser, "Function calls not yet implemented")
        } else {
            assignment(parser, compiler, last, 1)
        }
    case .Print:
        print_stmt(parser, compiler)
    case .Do:
        // active := sa.len(compiler.active)
        compiler_begin_scope(compiler)
        block(parser, compiler)
        compiler_end_scope(compiler)
    case .Local:
        local_stmt(parser, compiler)
    case:
        error_at(parser, parser.consumed, "Expected an expression")
    }
    // Optional
    parser_match(parser, .Semicolon)
}

/*
**Analogous to**
-   `compiler.c:varDeclaration()` in the book.
-   `compiler.c:parseVariable()` (somewhat) in the book.

**Links**
-   https://www.lua.org/source/5.1/lparser.c.html#exprstat
-   https://www.lua.org/source/5.1/lparser.c.html#prefixexp
-   https://www.lua.org/source/5.1/lparser.c.html#singlevar
 */
@(private="file")
assignment :: proc(parser: ^Parser, compiler: ^Compiler, last: ^LValue, n_vars: int) {
    // Don't call `variable()` for the first assignment because we did so already
    // to check for function calls.
    if n_vars > 1 {
        last.variable = variable(parser, compiler)
    }

    // Use recursive calls to create a stack-allocated linked list.
    if parser_match(parser, .Comma) {
        parser_consume(parser, .Identifier)
        parser_recurse_begin(parser)
        assignment(parser, compiler, &LValue{prev = last}, n_vars + 1)
        parser_recurse_end(parser)
        // Parents of recursive calls will always go to base cases because their
        // values are guaranteed to be pushed to the stack.
    } else {
        parser_consume(parser, .Equals)

        /*
        **Notes** (2025-04-18):
        -   We don't want to immediately push `expr`. This is an optimization
            mainly for the last occurence of `Expr_Type.Table_Index`.
        -   We want to handle each recursive call's associated expression.
         */
        expr, n_exprs := expr_list(parser, compiler)
        if n_exprs == n_vars {
            // luaK_setoneret(ls->fs, &e)
            compiler_store_var(compiler, &last.variable, &expr)
            return // Avoid base case to prevent needless popping.
        }
        adjust_assign(compiler, n_vars, n_exprs, &expr)
        // Go to base case as our value is on the top of the stack.
    }

    /*
    **Overview**
    -   Base case. We just push to the next register no matter what.

    **Assumptions**
    -   `expr_list()` pushed all expressions except the last.

    **Guarantees**
    -   As we unwind the recursive call stack, we keep decrementing
        `compiler.free_reg`.
    -   `compiler.free_reg - 1` (the current top) is the register of the desired
        value for the current assignment target.
     */
    expr := expr_make(.Discharged, reg = cast(u16)compiler.free_reg - 1)
    compiler_store_var(compiler, &last.variable, &expr)
}


/*
**Analogous to**
-   `compiler.c:identifierConstant(Token *name)` in the book.
*/
@(private="file")
ident_constant :: proc(parser: ^Parser, compiler: ^Compiler, token: Token) -> (ident: ^OString, index: u32) {
    ident = ostring_new(parser.vm, token.lexeme)
    value := value_make(ident)
    return ident, compiler_add_constant(compiler, value)
}

/*
**Form**
-   local_stmt ::= 'local' local_decl [ '=' expr_list ]

**Notes**
-   Due to the differences in Lua and Lox, we cannot combine local variable
    declarations into our `parseVariable()` analog as we do not have a
    catch-all `var` keyword.
*/
@(private="file")
local_stmt :: proc(parser: ^Parser, compiler: ^Compiler) {
    count_vars: int
    for {
        defer count_vars += 1

        parser_consume(parser, .Identifier)
        // Don't call `ident_constant()` because we don't need to pollute the
        // constants array.
        ident := ostring_new(parser.vm, parser.consumed.lexeme)
        local_decl(parser, compiler, ident, count_vars)
        if !parser_match(parser, .Comma) {
            break
        }
    }

    expr: Expr
    count_exprs: int
    // No need for `else` clause as zero value is already .Empty
    if parser_match(parser, .Equals) {
        expr, count_exprs = expr_list(parser, compiler)
    }

    /*
    Normally, we want to ensure zero stack effect. However, in this case,
    we want the initialization expressions to remain on the stack as they
    will act as the local variables themselves.
    */
    adjust_assign(compiler, count_vars, count_exprs, &expr)
    local_adjust(compiler, count_vars)
}


/*
**Form**
-   local_decl ::= identifier [',' identifier]*

**Analogous to**
-   `compiler.c:declareVariable()` in the book.
-   `lparser.c:new_localvar(LexState *ls, TString *name, int n)` in Lua 5.1.5.
 */
@(private="file")
local_decl :: proc(parser: ^Parser, compiler: ^Compiler, ident: ^OString, counter: int) {
    chunk  := compiler.chunk
    depth  := compiler.scope_depth
    locals := chunk.locals
    #reverse for active in sa.slice(&compiler.active) {
        local := locals[active]
        // Already poking at initialized locals in outer scopes?
        if local.depth < depth {
            break
        }
        if local.ident == ident {
            parser_error_consumed(parser, "Shadowing of local variable")
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
    active_reg := sa.len(compiler.active) + counter
    if active_reg >= MAX_LOCALS {
        buf: [64]byte
        msg := fmt.bprintf(buf[:], "More than %i local variables", MAX_LOCALS)
        parser_error_consumed(parser, msg)
    }
    local_index := compiler_add_local(compiler, ident)
    sa.set(&compiler.active, index = active_reg, item = local_index)
}

/*
**Notes**
-   See `lparser.c:adjust_assign(LexState *ls, int nvars, int nexps, expdesc *e)`.
 */
@(private="file")
adjust_assign :: proc(compiler: ^Compiler, count_vars, count_exprs: int, expr: ^Expr) {
    // TODO(2025-04-08): Add `if (hasmultret(expr->kind))` analog

    // Emit the last expression from `expr_list()`.
    if expr.type != .Empty {
        compiler_expr_next_reg(compiler, expr)
    }

    // More variables than expressions?
    if extra := count_vars - count_exprs; extra > 0 {
        reg := compiler.free_reg
        compiler_reserve_reg(compiler, extra)
        compiler_code_nil(compiler, cast(u16)reg, cast(u16)extra)
    } else {
        /*
        **Sample**
        -   local a, b, c = 1, 2, 3, 4

        **Results**
        -   free_reg    = 4
        -   count_vars  = 3
        -   count_exprs = 4

        **Assumptions**
        -   If `count_exprs == count_vars`, nothing changes as we subtract 0.
         */
        compiler.free_reg -= count_exprs - count_vars
    }
}


/*
**Analogous to**
-   `lparser.c:adjustlocalvars(LexState *ls, int nvars)` in Lua 5.1.5.

**Notes**
-   We don't need a `remove_locals()` function because `compiler_end_scope()`
    takes care of that already.
 */
@(private="file")
local_adjust :: proc(compiler: ^Compiler, nvars: int) {
    startpc := compiler.chunk.pc
    depth   := compiler.scope_depth

    /*
    **Assumptions**
    -   This relies on the next `nvars` elements in the array having been set
        previously by `local_decl`.
    -   `compiler.active.len` was NOT incremented yet.
     */
    nactive := sa.len(compiler.active) + nvars
    sa.resize(&compiler.active, nactive)
    active := sa.slice(&compiler.active)
    locals := compiler.chunk.locals
    for i := nvars; i > 0; i -= 1 {
        // `lparser.c:getlocvar(fs, i)`
        index := active[nactive - i]
        local := &locals[index]
        local.depth   = depth
        local.startpc = startpc
    }
}

@(private="file")
block :: proc(parser: ^Parser, compiler: ^Compiler) {
    for !parser_check(parser, .End) && !parser_check(parser, .Eof) {
        parser_parse(parser, compiler)
    }
    parser_consume(parser, .End)
}


/*
**Notes**
-   See `lparser.c:funcargs(LexState *ls, expdesc *e)`.
*/
@(private="file")
print_stmt :: proc(parser: ^Parser, compiler: ^Compiler) {
    compiler.is_print = true
    defer compiler.is_print = false

    parser_consume(parser, .Left_Paren)

    args: Expr
    count_args: int
    if !parser_match(parser, .Right_Paren) {
        args, count_args = expr_list(parser, compiler)
        parser_consume(parser, .Right_Paren)
    }

    // Emit the last expression from `expr_list()`.
    if args.type != .Empty {
        compiler_expr_next_reg(compiler, &args)
    }

    compiler_code_AB(compiler, .Print,
        cast(u16)(compiler.free_reg - count_args), cast(u16)compiler.free_reg)

    // This is hacky but it works to allow recycling of registers
    compiler.free_reg -= count_args
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
@(private="file")
expr_list :: proc(parser: ^Parser, compiler: ^Compiler) -> (expr: Expr, count: int) {
    count = 1 // at least one expression
    expr  = expression(parser, compiler)
    for parser_match(parser, .Comma) {
        compiler_expr_next_reg(compiler, &expr)
        expr = expression(parser, compiler)
        count += 1
    }
    return expr, count
}

/*
**Form**
-   expression ::= literal | unary | grouping | binary | variable

**Analogous to**
-   `compiler.c:expression()` in the book.

**Notes**
-   Expressions only ever produce 1 net resulting value, which should reside in `expr`.
-   However, `expr` itself does not reside in a register yet. It is up to you
    to decide how to allocate that.
 */
@(private="file")
expression :: proc(parser: ^Parser, compiler: ^Compiler) -> (expr: Expr) {
    return parse_precedence(parser, compiler, .None + Precedence(1))
}


/*
**Analogous to**
-   'lparser.c:subexpr(LexState *ls, expdesc *v, int limit)' in Lua 5.1.5.

**Links**
-   https://www.lua.org/source/5.1/lparser.c.html#subexpr
 */
parse_precedence :: proc(parser: ^Parser, compiler: ^Compiler, prec: Precedence) -> Expr {
    parser_recurse_begin(parser)
    defer parser_recurse_end(parser)

    parser_advance(parser)
    prefix := get_rule(parser.consumed.type).prefix
    if prefix == nil {
        parser_error_consumed(parser, "Expected an expression")
    }

    // Prefix expressions are always the "root" node. We don't know if we're in
    // a recursive call.
    expr := prefix(parser, compiler)
    for {
        rule := get_rule(parser.lookahead.type)
        if prec > rule.prec {
            break
        }
        // Can occur when we hardcode low precedence recursion in high precedence calls
        assert(rule.infix != nil)
        parser_advance(parser)

        // Infix expressions are the actual branches.
        rule.infix(parser, compiler, &expr)
    }

    return expr
}


/*
**Analogous to**
-   'compiler.c:errorAtCurrent()' in the book.
 */
parser_error_lookahead :: proc(parser: ^Parser, msg: string) -> ! {
    error_at(parser, parser.lookahead, msg)
}


/*
**Analogous to**
-   'compiler.c:error()' in the book.
 */
parser_error_consumed :: proc(parser: ^Parser, msg: string) -> ! {
    error_at(parser, parser.consumed, msg)
}


/*
**Analogous to**
-   'compiler.c:errorAt()' in the book.
 */
@(private="file")
error_at :: proc(parser: ^Parser, token: Token, msg: string) -> ! {
    vm     := parser.vm
    source := parser.lexer.source
    line   := token.line
    // .Eof token: don't use lexeme as it'll just be an empty string.
    location := token.lexeme if token.type != .Eof else token_type_strings[.Eof]
    vm_compile_error(vm, source, line, "%s at '%s'", msg, location)
}


/// PREFIX EXPRESSIONS


/*
**Form**
-   grouping ::= '(' expression ')'
 */
@(private="file")
grouping :: proc(parser: ^Parser, compiler: ^Compiler) -> Expr {
    expr := expression(parser, compiler)
    parser_consume(parser, .Right_Paren)
    return expr
}

parser_recurse_begin :: proc(parser: ^Parser) {
    parser.recurse += 1
    if parser.recurse >= PARSER_MAX_RECURSE {
        parser_error_consumed(parser, "Too many syntax levels")
    }
}

parser_recurse_end :: proc(parser: ^Parser) {
    parser.recurse -= 1
}


/*
**Form**
-   literal ::= 'nil' | 'true' | 'false' | NUMBER | STRING
 */
@(private="file")
literal :: proc(parser: ^Parser, compiler: ^Compiler) -> Expr {
    token := parser.consumed
    value := token.literal
    #partial switch token.type {
    case .Nil:      return expr_make(.Nil)
    case .True:     return expr_make(.True)
    case .False:    return expr_make(.False)
    case .Number:   return expr_make(.Number, value.(f64))
    case .String:
        index := compiler_add_constant(compiler, value_make(value.(^OString)))
        return expr_make(.Constant, index = index)
    case:
        unreachable("Token type %v is not a literal", token.type)
    }
}

/*
**Form**
-   variable ::= identifier [ indexed | ( '.' identifier ) ]*

**Assumptions**
-   `parser.consumed` is of type `.Identifier`.

**Notes:**
-   See `lparser.c:singlevar(LexState *ls, Expr *var)`.
-   See `lparser.c:singlevaraux(LexState *ls, TString *n, Expr *var, int (bool) base)`
 */
@(private="file")
variable :: proc(parser: ^Parser, compiler: ^Compiler) -> Expr {
    /*
    **Overview**
    -   Inline implementation of `compiler.c:namedVariable(Token name)` in the book.

    **Notes** (2025-04-18):
    -   We don't call `ident_constant()` yet because we don't want
     */

    first_var :: proc(parser: ^Parser, compiler: ^Compiler) -> Expr {
        ident := ostring_new(parser.vm, parser.consumed.lexeme)
        if local, ok := compiler_resolve_local(compiler, ident); ok {
            return expr_make(.Local, reg = local)
        }
        index := compiler_add_constant(compiler, value_make_string(ident))
        return expr_make(.Global, index = index)
    }

    var := first_var(parser, compiler)
    table_fields: for {
        prev := parser.consumed
        parser_advance(parser)
        #partial switch parser.consumed.type {
        case .Left_Bracket:
            /*
            **Overview**
            -   Emit the parent table of this index.

            **Notes** (2025-04-18):
            -   If it's a local then just reuse the register.
            -   If it's a constant then try to use RK, else push it.
             */
            compiler_expr_any_reg(compiler, &var)
            key := indexed(parser, compiler)
            compiler_code_indexed(compiler, &var, &key)
        case .Period:
            // Same idea as in `.Left_Bracket` case.
            compiler_expr_any_reg(compiler, &var)
            parser_consume(parser, .Identifier)
            key := field_name(parser, compiler)
            compiler_code_indexed(compiler, &var, &key)
        case .Colon:
            parser_error_consumed(parser, "':' syntax not yet supported")
        case:
            parser_backtrack(parser, prev)
            break table_fields
        }
    }
    return var
}

/*
**Form**
-   indexed ::= '[' expression ']'

**Overview**
-   Compiles an expression and saves it to a new `Expr` instance.
-   This instance represents either a get-operation or a literal.

**Analogous to**
-   `lparser.c:yindex(LexState *ls, expdesc *var)` in Lua 5.1.5.
 */
@(private="file")
indexed :: proc(parser: ^Parser, compiler: ^Compiler) -> (key: Expr) {
    key = expression(parser, compiler)
    compiler_expr_to_value(compiler, &key)
    parser_consume(parser, .Right_Bracket)
    return key
}


/*
**Form**
-   field_name ::= identifier

**Overview**
-   Save fieldname in an expression which we can emit as an RK.

**Assumptions**
-   The desired field name (an `.Identifier`) was just consumed.

**Notes**
-   If the index does not fit in an RK, you will have to push it yourself!
 */
@(private="file")
field_name :: proc(parser: ^Parser, compiler: ^Compiler) -> (key: Expr) {
    _, index := ident_constant(parser, compiler, parser.consumed)
    return expr_make(.Constant, index = index)
}


/*
**Notes**
-   See the `lparser.c:ConsControl` structure in Lua 5.1.5.
 */
Constructor :: struct {
    table: Expr, // table descriptor
    count_array, count_hash: int,
    to_store: int, // number of array elements pending to be stored
}


/*
**Form**
-   table ::= '{' table_element? [ ',' table_element ]* '}'
    table_element ::= expression
                    | ( indexed | identifier) '=' expression


**Assumptions**
-   The `{` token was just consumed.
 */
@(private="file")
constructor :: proc(parser: ^Parser, compiler: ^Compiler) -> Expr {

    /*
    **Analogous to**
    -   `lparser.c:closelistfield(LexState *ls, struct ConsControl *cc)` in
        Lua 5.1.5.
     */
    array :: proc(parser: ^Parser, compiler: ^Compiler, ctor: ^Constructor) {
        defer {
            ctor.count_array += 1
            ctor.to_store    += 1
        }

        value := expression(parser, compiler)
        compiler_expr_next_reg(compiler, &value)
    }

    /*
    **Assumptions**
    -   The `.Equals` token was NOT yet consumed, it should still be the
        desired field name.
     */
    field :: proc(parser: ^Parser, compiler: ^Compiler, ctor: ^Constructor) {
        defer ctor.count_hash += 1

        key := field_name(parser, compiler)
        parser_consume(parser, .Equals)
        b := compiler_expr_regconst(compiler, &key)

        value := expression(parser, compiler)
        c := compiler_expr_regconst(compiler, &value)
        compiler_code_ABC(compiler, .Set_Table, ctor.table.reg, b, c)
        compiler_expr_pop(compiler, value)
        compiler_expr_pop(compiler, key)

    }

    index :: proc(parser: ^Parser, compiler: ^Compiler, ctor: ^Constructor) {
        defer ctor.count_hash += 1

        key := expression(parser, compiler)
        parser_consume(parser, .Right_Bracket)
        b := compiler_expr_regconst(compiler, &key)

        parser_consume(parser, .Equals)

        value := expression(parser, compiler)
        c := compiler_expr_regconst(compiler, &value)

        compiler_code_ABC(compiler, .Set_Table, ctor.table.reg, b, c)
        // Reuse these registers
        compiler_expr_pop(compiler, value)
        compiler_expr_pop(compiler, key)
    }

    // All information is pending so just use 0's, we'll fix it later
    pc := compiler_code_ABC(compiler, .New_Table, 0, 0, 0)
    ctor := &Constructor{table = expr_make(.Need_Register, pc = pc )}
    compiler_expr_next_reg(compiler, &ctor.table)

    for !parser_match(parser, .Right_Curly) {
        /*
        **Analogous to**
        -   `lparser.c:closelistfield(LexState *ls, struct ConsControl *cc)`

        **Assumptions**
        -   This is an inline implementation.
        -   `array` already pushed each expression.
         */
        if ctor.to_store == FIELDS_PER_FLUSH {
            compiler_code_set_array(compiler, ctor.table.reg,
                ctor.count_array, ctor.to_store)
            ctor.to_store = 0
        }

        // for backtracking
        saved_consumed := parser.consumed
        parser_advance(parser)
        #partial switch(parser.consumed.type) {
        case .Identifier:
            // `.Equals` is consumed inside of `field`
            if parser_check(parser, .Equals) {
                field(parser, compiler, ctor)
            } else {
                parser_backtrack(parser, saved_consumed)
                array(parser, compiler, ctor)
            }
        case .Left_Bracket:
            index(parser, compiler, ctor)
        case:
            parser_backtrack(parser, saved_consumed)
            array(parser, compiler, ctor)
        }

        if !parser_match(parser, .Comma) {
            parser_consume(parser, .Right_Curly)
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
        compiler_code_set_array(compiler, ctor.table.reg, ctor.count_array,
            ctor.to_store)
    }

    // `fb_from_int()` may also round up the values by some factor, but that's
    // okay because our hash table will simply over-allocate.
    ip := &compiler.chunk.code[pc]
    ip.b = cast(u16)fb_from_int(ctor.count_array)
    ip.c = cast(u16)fb_from_int(ctor.count_hash)

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
@(private="file")
unary :: proc(parser: ^Parser, compiler: ^Compiler) -> Expr {
    type := parser.consumed.type

    // Compile the operand. We know the first token of the operand is in the lookahead.
    expr := parse_precedence(parser, compiler, .Unary)

    /*
    **Links**
    -   https://www.lua.org/source/5.1/lcode.c.html#luaK_prefix
    -   https://the-ravi-programming-language.readthedocs.io/en/latest/lua-parser.html#state-transitions

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
            if !(.Nil <= expr.type && expr.type <= .Number) {
                compiler_expr_any_reg(compiler, &expr)
            }
        } else {
            // If nested (e.g. `-(-x)`) reuse the register we stored `x` in
            compiler_expr_any_reg(compiler, &expr)
        }
        // MUST be set to '.Number' in order to try constant folding.
        dummy := expr_make(.Number)
        compiler_code_arith(compiler, .Unm, &expr, &dummy)
    case .Not:
        compiler_code_not(compiler, &expr)
    case .Pound:
        // OpCode.Len CANNOT operate on constants no matter what.
        compiler_expr_any_reg(compiler, &expr)
        dummy := expr_make(.Number)
        compiler_code_arith(compiler, .Len, &expr, &dummy)
    case:
        unreachable("Token %v is not an unary operator", type)
    }

    return expr
}

/// INFIX EXPRESSIONS


/*
**Form**
-   arith    ::= arith_op expression
    arith_op ::= '+' | '-' | '*' | '/' | '%' | '^'

**Notes**
-   '..' is not included due to its unique semantics: neither arguments B nor C
    can be RK.
 */
@(private="file")
arith :: proc(parser: ^Parser, compiler: ^Compiler, left: ^Expr) {
    type := parser.consumed.type
    op: OpCode
    #partial switch type {
    // Arithmetic
    case .Plus:    op = .Add
    case .Dash:    op = .Sub
    case .Star:    op = .Mul
    case .Slash:   op = .Div
    case .Percent: op = .Mod
    case .Caret:   op = .Pow

    // Misc.
    case: unreachable("Invalid binary operator %v", type)
    }

    if USE_CONSTANT_FOLDING {
        if !expr_is_number(left^) {
            compiler_expr_regconst(compiler, left)
        }
    } else {
        /*
        NOTE(2025-01-19):
        -   This is necessary when both sides are nonconstant binary expressions!
            e.g. `'h' .. 'i' == 'h' .. 'i'`
        -   This is because, by itself, expressions like `concat` result in
            `.Need_Register`.
         */
        compiler_expr_regconst(compiler, left)
    }

    prec := get_rule(type).prec

    // By not adding 1 for exponentiation we enforce right-associativity since we
    // keep emitting the ones to the right first
    if prec != .Exponent {
        prec += Precedence(1)
    }

    right := parse_precedence(parser, compiler, prec)

    /*
    **Note**
    -   This is effectively the inline implementation of the only relevant line/s
        from 'lcode.c:luaK_posfix()'.

    **Links**
    -   https://www.lua.org/source/5.1/lcode.c.html#luaK_posfix
     */
    compiler_code_arith(compiler, op, left, &right)
}

/*
**Form**
-   compare    ::= compare_op expression
    compare_op ::= '==' | '<' | '<=' | '~=' | '>=' | '>'

*/
compare :: proc(parser: ^Parser, compiler: ^Compiler, left: ^Expr) {
    type := parser.consumed.type
    inverted := false
    op: OpCode
    #partial switch type {
    case .Tilde_Eq:       op = .Neq
    case .Equals_2:       op = .Eq

    // (x > y) == (y < x)
    case .Right_Angle:    inverted = true; fallthrough
    case .Left_Angle:     op = .Lt

    // (x >= y) == (y <= x)
    case .Right_Angle_Eq: inverted = true; fallthrough
    case .Left_Angle_Eq:  op = .Leq
    case:
        unreachable("Token %v is not a comparison operator", type)
    }

    compiler_expr_regconst(compiler, left)
    prec  := get_rule(type).prec
    right := parse_precedence(parser, compiler, prec)
    compiler_code_compare(compiler, op, inverted, left, &right)
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
@(private="file")
concat :: proc(parser: ^Parser, compiler: ^Compiler, left: ^Expr) {
    // Left-hand operand MUST be on the stack
    compiler_expr_next_reg(compiler, left)

    // If recursive concat, this will be `.Need_Register` as well.
    right := parse_precedence(parser, compiler, .Concat)
    compiler_code_concat(compiler, left, &right)
}

get_rule :: proc(type: Token_Type) -> (rule: Parse_Rule) {
    @(static, rodata)
    rules := #partial [Token_Type]Parse_Rule {
        // Keywords
        .False      = {prefix = literal},
        .Nil        = {prefix = literal},
        .Not        = {prefix = unary},
        .True       = {prefix = literal},

        // Balanced Pairs
        .Left_Paren = {prefix = grouping},
        .Left_Curly = {prefix = constructor},

        // Arithmetic
        .Plus       = {infix = arith, prec = .Terminal},
        .Dash       = {prefix = unary, infix = arith, prec = .Terminal},
        .Star       = {infix = arith, prec = .Factor},
        .Slash      = {infix = arith, prec = .Factor},
        .Percent    = {infix = arith, prec = .Factor},
        .Caret      = {infix = arith, prec = .Exponent},

        // Comparison
        .Equals_2       = {infix = compare, prec = .Equality},
        .Tilde_Eq       = {infix = compare, prec = .Equality},
        .Left_Angle     = {infix = compare, prec = .Comparison},
        .Left_Angle_Eq  = {infix = compare, prec = .Comparison},
        .Right_Angle    = {infix = compare, prec = .Comparison},
        .Right_Angle_Eq = {infix = compare, prec = .Comparison},

        // Other
        .Ellipsis_2 = {infix = concat, prec = .Concat},
        .Pound      = {prefix = unary},

        // Literals
        .Number     = {prefix = literal},
        .String     = {prefix = literal},
        .Identifier = {prefix = variable},
    }
    return rules[type]
}

