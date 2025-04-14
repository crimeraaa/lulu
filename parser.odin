#+private
package lulu

import "core:fmt"
import "core:strings"

// https://www.lua.org/source/5.1/luaconf.h.html#LUAI_MAXCCALLS
PARSER_MAX_RECURSE :: 200

Parser :: struct {
    vm:                 ^VM,
    lexer:               Lexer,
    consumed, lookahead: Token,
    recurse:             int,
}

Parse_Rule :: struct {
    prefix, infix: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr),
    prec:          Precedence,
}

/*
Links:
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
Analogous to:
-   'compiler.c:advance()' in the book.
 */
parser_advance :: proc(parser: ^Parser) {
    token := lexer_scan_token(&parser.lexer)
    parser.consumed, parser.lookahead = parser.lookahead, token
}


/*
Assumptions:
-   `previous` is the token right before `parser.consumed`.
-   Is not called multiple times in a row.

Guarantees:
-   `parser.lexer` points to the start of the lexeme before `parser.lookahead`.
-   When we call `parser_advance()`, we end up back at the old lookahead.
 */
parser_backtrack :: proc(parser: ^Parser, previous: Token) {
    lexer := &parser.lexer
    lexer.start   -= len(parser.lookahead.lexeme)
    lexer.current = lexer.start
    lexer.line    = previous.line

    parser.consumed, parser.lookahead = previous, parser.consumed

}


/*
Analogous to:
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
    found = (parser.lookahead.type == expected)
    if found do parser_advance(parser)
    return found
}

parser_check :: proc(parser: ^Parser, expected: Token_Type) -> (found: bool) {
    found = (parser.lookahead.type == expected)
    return found
}

LValue :: struct {
    prev:    ^LValue,
    variable: Expr,
}

/*
Analogous to:
-   `compiler.c:declaration()` in the book.
-   `lparser.c:chunk(LexState *ls)` in Lua 5.1.5.

Links:
-   https://www.lua.org/source/5.1/lparser.c.html#chunk
 */
parser_parse :: proc(parser: ^Parser, compiler: ^Compiler) {
    switch {
    case parser_match(parser, .Identifier):
        last := &LValue{}
        // Inline implementation of `compiler.c:parseVariable()` since we immediately
        // consumed the 'identifier'. Also, Lua doesn't have a 'var' keyword.
        variable(parser, compiler, &last.variable)

        if parser_match(parser, .Left_Paren) {
            parser_error_consumed(parser, "Function calls not yet implemented")
        } else {
            assignment(parser, compiler, last, 1)
        }
    case:
        statement(parser, compiler)
    }
    // Optional
    parser_match(parser, .Semicolon)
}

/*
Analogous to:
-   `compiler.c:varDeclaration()` in the book.
-   `compiler.c:parseVariable()` (somewhat) in the book.

Links:
-   https://www.lua.org/source/5.1/lparser.c.html#exprstat
-   https://www.lua.org/source/5.1/lparser.c.html#prefixexp
-   https://www.lua.org/source/5.1/lparser.c.html#singlevar
 */
@(private="file")
assignment :: proc(parser: ^Parser, compiler: ^Compiler, last: ^LValue, count_vars: int) {
    // Don't call `variable()` for the first assignment because we did so already
    // to check for function calls.
    if count_vars > 1 do variable(parser, compiler, &last.variable)

    // Use recursive calls to create a stack-allocated linked list.
    if parser_match(parser, .Comma) {
        parser_consume(parser, .Identifier)
        assignment(parser, compiler, &LValue{prev = last}, count_vars + 1)
        return // Prevent parents of recursive calls from consuming '='
    }
    parser_consume(parser, .Equals)

    expr, count_exprs := expr_list(parser, compiler)
    adjust_assign(compiler, count_vars, count_exprs, &expr) // Must come first!

    // Register of the value we will use to assign a particlar `LValue`.
    // a, b, c = 1, 2, 3; free_reg = 3; count_exprs = 3; reg = 2
    reg := cast(u16)compiler.free_reg - 1

    // Assign going downwards and free in the correct order so that these
    // registers can be reused.
    iter := last
    for current in lvalue_iterator(&iter) {
        defer reg -= 1

        var := current.variable
        #partial switch type := var.type; type {
        case .Global:
            // reg = value; var.index = name
            compiler_emit_ABx(compiler, .Set_Global, reg, var.index)
        case .Local:
            // var.reg = local; reg = value;
            compiler_emit_ABC(compiler, .Move, var.reg, reg, 0)
        case .Table_Index:
            // var.table.reg = table; var.table.index = key; reg = value;
            compiler_emit_ABC(compiler, .Set_Table, var.table.reg, var.table.index, reg)
        case:
            fmt.panicf("Impossible assignment target: %v", type)
        }
        // TODO(2025-04-14): Maybe we can just pop `count_vars` directly?
        compiler_pop_reg(compiler, reg)
    }
}

lvalue_iterator :: proc(iter: ^^LValue) -> (current: ^LValue, ok: bool) {
    current = iter^
    if current == nil do return nil, false
    iter^ = current.prev
    return current, true
}

/*
Analogous to:
-   `compiler.c:identifierConstant(Token *name)` in the book.
*/
@(private="file")
ident_constant :: proc(parser: ^Parser, compiler: ^Compiler, token: Token) -> (ident: ^OString, index: u32) {
    ident = ostring_new(parser.vm, token.lexeme)
    value := value_make_string(ident)
    return ident, compiler_add_constant(compiler, value)
}


/*
Analogous to:
-   `compiler.c:statement()` in the book.
-   `lparser.c:statement(LexState *ls)` in Lua 5.1.5.

Links:
-   https://www.lua.org/source/5.1/lparser.c.html#statement
 */
@(private="file")
statement :: proc(parser: ^Parser, compiler: ^Compiler) {
    // line := parser.lookahead.line
    switch {
    case parser_match(parser, .Print):
        print_stmt(parser, compiler)
    case parser_match(parser, .Do):
        compiler_begin_scope(compiler)
        block(parser, compiler)
        compiler_end_scope(compiler)
    case parser_match(parser, .Local):
        local_stmt(parser, compiler)
    case:
        error_at(parser, parser.lookahead, "Expected an expression")
    }
}


/*
Form:
-   local_stmt ::= 'local' local_decl [ '=' expr_list ]

Notes:
-   Due to the differences in Lua and Lox, we cannot combine local variable
    declarations into our `parseVariable()` analog as we do not have a
    catch-all `var` keyword.
*/
@(private="file")
local_stmt :: proc(parser: ^Parser, compiler: ^Compiler) {
    count_vars: int
    for {
        parser_consume(parser, .Identifier)
        ident, _ := ident_constant(parser, compiler, parser.consumed)
        local_decl(parser, compiler, ident)
        count_vars += 1
        if !parser_match(parser, .Comma) do break
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
Form:
-   local_decl ::= identifier [',' identifier]*

Analogous to:
-   `compiler.c:declareVariable()` in the book.
-   `lparser.c:new_localvar(LexState *ls, TString *name, int n)` in Lua 5.1.5.
 */
@(private="file")
local_decl :: proc(parser: ^Parser, compiler: ^Compiler, ident: ^OString) {
    if chunk_check_shadowing(compiler.chunk, ident, compiler.scope_depth) {
        parser_error_consumed(parser, "Shadowing of local variable")
    }
    compiler_add_local(compiler, ident)
}

/*
Notes:
-   See `lparser.c:adjust_assign(LexState *ls, int nvars, int nexps, expdesc *e)`.
 */
@(private="file")
adjust_assign :: proc(compiler: ^Compiler, count_vars, count_exprs: int, expr: ^Expr) {
    // TODO(2025-04-08): Add `if (hasmultret(expr->kind))` analog

    // Emit the last expression from `expr_list()`.
    if expr.type != .Empty do compiler_expr_next_reg(compiler, expr)

    // More variables than expressions?
    if extra := count_vars - count_exprs; extra > 0 {
        reg := compiler.free_reg
        compiler_reserve_reg(compiler, extra)
        compiler_emit_nil(compiler, cast(u16)reg, cast(u16)extra)
    } else {
        /*
        Sample:
        -   local a, b, c = 1, 2, 3, 4

        Results:
        -   free_reg    = 4
        -   count_vars  = 3
        -   count_exprs = 4

        Assumptions:
        -   If `count_exprs == count_vars`, nothing changes as we subtract 0.
         */
        compiler.free_reg -= count_exprs - count_vars
    }

}


/*
Notes:
-   We don't need a `remove_locals()` function because `compiler_end_scope()`
    takes care of that already.
 */
@(private="file")
local_adjust :: proc(compiler: ^Compiler, nvars: int) {
    // startpc := compiler.chunk.pc
    depth := compiler.scope_depth

    // NOTE(2025-04-09): This is VERY important!
    compiler.active_local += nvars
    for i := nvars; i > 0; i -= 1 {
        compiler.chunk.locals[compiler.active_local - i].depth = depth
        // compiler.locals[compiler.count_local - i].startpc = startpc
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

Notes:
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
    if args.type != .Empty do compiler_expr_next_reg(compiler, &args)

    compiler_emit_AB(compiler, .Print,
        cast(u16)(compiler.free_reg - count_args), cast(u16)compiler.free_reg)

    // This is hacky but it works to allow recycling of registers
    compiler.free_reg -= count_args
}

/*
Form:
-   expr_list ::= expression [',' expression]*

Overview:
-   Pushes a comma-separated list of expressions onto the stack, save for the
    last expression.

Notes:
-   Like in Lua 5.1.5, the last expression is not emitted.
 */
@(private="file")
expr_list :: proc(parser: ^Parser, compiler: ^Compiler) -> (expr: Expr, count: int) {
    count = 1 // at least one expression
    expr = expression(parser, compiler)
    for parser_match(parser, .Comma) {
        compiler_expr_next_reg(compiler, &expr)
        expr = expression(parser, compiler)
        count += 1
    }
    return expr, count
}

/*
Form:
-   expression ::= literal | unary | grouping | binary | variable

Analogous to:
-   `compiler.c:expression()` in the book.

Notes:
-   Expressions only ever produce 1 net resulting value, which should reside in `expr`.
-   However, `expr` itself does not reside in a register yet. It is up to you
    to decide how to allocate that.
 */
@(private="file")
expression :: proc(parser: ^Parser, compiler: ^Compiler) -> (expr: Expr) {
    return parse_precedence(parser, compiler, .None + Precedence(1))
}


/*
Analogous to:
-   'lparser.c:subexpr(LexState *ls, expdesc *v, int limit)' in Lua 5.1.5.

Links:
-   https://www.lua.org/source/5.1/lparser.c.html#subexpr
 */
parse_precedence :: proc(parser: ^Parser, compiler: ^Compiler, prec: Precedence) -> (expr: Expr) {
    parser_recurse_begin(parser)
    defer parser_recurse_end(parser)

    parser_advance(parser)
    prefix := get_rule(parser.consumed.type).prefix
    if prefix == nil {
        parser_error_consumed(parser, "Expected an expression")
    }
    prefix(parser, compiler, &expr)

    for {
        rule := get_rule(parser.lookahead.type)
        if prec > rule.prec do break
        // Can occur when we hardcode low precedence recursion in high precedence calls
        assert(rule.infix != nil)
        parser_advance(parser)
        rule.infix(parser, compiler, &expr)
    }

    return expr
}


/*
Analogous to:
-   'compiler.c:errorAtCurrent()' in the book.
 */
parser_error_lookahead :: proc(parser: ^Parser, msg: string) -> ! {
    error_at(parser, parser.lookahead, msg)
}


/*
Analogous to:
-   'compiler.c:error()' in the book.
 */
parser_error_consumed :: proc(parser: ^Parser, msg: string) -> ! {
    error_at(parser, parser.consumed, msg)
}


/*
Analogous to:
-   'compiler.c:errorAt()' in the book.
 */
@(private="file")
error_at :: proc(parser: ^Parser, token: Token, msg: string) -> ! {
    // .Eof token: don't use lexeme as it'll just be an empty string.
    location := token.lexeme if token.type != .Eof else token_type_strings[.Eof]
    vm_compile_error(parser.vm, parser.lexer.source, token.line, "%s at '%s'", msg, location)
}


/// PREFIX EXPRESSIONS


/*
Form:
-   grouping ::= '(' expression ')'
 */
@(private="file")
grouping :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    expr^ = expression(parser, compiler)
    parser_consume(parser, .Right_Paren)
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
Form:
-   literal ::= 'nil' | 'true' | 'false' | NUMBER | STRING
 */
@(private="file")
literal :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    token := parser.consumed
    value := token.literal
    #partial switch token.type {
    case .Nil:      expr_init(expr, .Nil)
    case .True:     expr_init(expr, .True)
    case .False:    expr_init(expr, .False)
    case .Number:   expr_set_number(expr, value.(f64))
    case .String:
        index := compiler_add_constant(compiler, value_make_string(value.(^OString)))
        expr_set_index(expr, .Constant, index)
    case: unreachable()
    }
}

/*
Form:
-   variable ::= identifier [ indexed | ( '.' identifier ) ]*

Assumptions:
-   `parser.consumed` is of type `.Identifier`.

Notes:
-   See `lparser.c:singlevar(LexState *ls, Expr *var)`.
-   See `lparser.c:singlevaraux(LexState *ls, TString *n, Expr *var, int (bool) base)`
 */
@(private="file")
variable :: proc(parser: ^Parser, compiler: ^Compiler, var: ^Expr) {
    // Inline implementation of `compiler.c:namedVariable(Token name)` in the book.
    ident, index := ident_constant(parser, compiler, parser.consumed)
    local, ok := compiler_resolve_local(compiler, ident)

    if ok do expr_set_reg(var, .Local, local)
    else do expr_set_index(var, .Global, index)

    table_fields: for {
        switch {
        case parser_match(parser, .Left_Bracket):
            // emit the parent table of this index
            compiler_expr_next_reg(compiler, var)
            key := indexed(parser, compiler)
            compiler_emit_indexed(compiler, var, &key)
        case parser_match(parser, .Period):
            // emit the parent table of this field
            compiler_expr_next_reg(compiler, var)
            parser_consume(parser, .Identifier)
            key := field_name(parser, compiler)
            compiler_emit_indexed(compiler, var, &key)
        case parser_match(parser, .Colon):
            parser_error_consumed(parser, "':' syntax not yet supported")
        case:
            break table_fields
        }
    }
}

/*
Form:
-   indexed ::= '[' expression ']'

Overview:
-   Compiles an expression and saves it to a new `Expr` instance.
-   This instance represents either a get-operation or a literal.

Analogous to:
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
Form:
-   field_name ::= identifier

Overview:
-   Save fieldname in an expression which we can emit as an RK.

Assumptions:
-   The desired field name (an `.Identifier`) was just consumed.

Notes:
-   If the index does not fit in an RK, you will have to push it yourself!
 */
@(private="file")
field_name :: proc(parser: ^Parser, compiler: ^Compiler) -> (key: Expr) {
    _, index := ident_constant(parser, compiler, parser.consumed)
    expr_set_index(&key, .Constant, index)
    return key
}


/*
Notes:
-   See the `lparser.c:ConsControl` structure in Lua 5.1.5.
 */
Constructor :: struct {
    table: ^Expr, // table descriptor
    count_array, count_hash: int,
    to_store: int, // number of array elements pending to be stored
}


/*
Form:
-   table ::= '{' table_element? [ ',' table_element ]* '}'
    table_element ::= expression
                    | ( indexed | identifier) '=' expression


Assumptions:
-   The `{` token was just consumed.
 */
@(private="file")
constructor :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    ctor := &Constructor{table = expr}
    // All information is pending so just use 0's, we'll fix it later
    pc := compiler_emit_ABC(compiler, .New_Table, 0, 0, 0)
    expr_set_pc(ctor.table, .Need_Register, pc)
    compiler_expr_next_reg(compiler, ctor.table)

    if !parser_check(parser, .Right_Curly) do for {
        // for backtracking
        saved_consumed := parser.consumed
        parser_advance(parser)
        #partial switch(parser.consumed.type) {
        case .Identifier:
            // `.Equals` is consumed inside of `ctor_field`
            if parser_check(parser, .Equals) {
                ctor_field(parser, compiler, ctor)
            } else {
                parser_backtrack(parser, saved_consumed)
                ctor_array(parser, compiler, ctor)
            }
        case .Left_Bracket:
            ctor_index(parser, compiler, ctor)
        case:
            parser_backtrack(parser, saved_consumed)
            ctor_array(parser, compiler, ctor)
        }

        if !parser_match(parser, .Comma) do break
    }

    parser_consume(parser, .Right_Curly)

    // `fb_from_int()` may also round up the values by some factor, but that's
    // okay because our hash table will simply over-allocate.
    compiler.chunk.code[pc].b = cast(u16)fb_from_int(ctor.count_array)
    compiler.chunk.code[pc].c = cast(u16)fb_from_int(ctor.count_hash)

    if count := ctor.count_array; count > 0 {
        // TODO(2025-04-13): Optimize for size! See `lopcodes.h:LFIELDS_PER_FLUSH`.
        compiler_emit_ABx(compiler, .Set_Array, ctor.table.reg, cast(u32)count)
        compiler.free_reg -= count
    }
}


@(private="file")
ctor_array :: proc(parser: ^Parser, compiler: ^Compiler, ctor: ^Constructor) {
    defer {
        ctor.count_array += 1
        ctor.to_store    += 1
    }

    value := expression(parser, compiler)
    compiler_expr_next_reg(compiler, &value)
}


/*
Assumptions:
-   The `.Equals` token was NOT yet consumed, it should still be the
    desired field name.
 */
@(private="file")
ctor_field :: proc(parser: ^Parser, compiler: ^Compiler, ctor: ^Constructor) {
    defer ctor.count_hash += 1

    key := field_name(parser, compiler)
    parser_consume(parser, .Equals)
    b := compiler_expr_regconst(compiler, &key)

    value := expression(parser, compiler)
    c := compiler_expr_regconst(compiler, &value)
    compiler_emit_ABC(compiler, .Set_Table, ctor.table.reg, b, c)
    compiler_expr_pop(compiler, value)
    compiler_expr_pop(compiler, key)

}


@(private="file")
ctor_index :: proc(parser: ^Parser, compiler: ^Compiler, ctor: ^Constructor) {
    defer ctor.count_hash += 1

    key := expression(parser, compiler)
    parser_consume(parser, .Right_Bracket)
    b := compiler_expr_regconst(compiler, &key)

    parser_consume(parser, .Equals)

    value := expression(parser, compiler)
    c := compiler_expr_regconst(compiler, &value)

    compiler_emit_ABC(compiler, .Set_Table, ctor.table.reg, b, c)
    // Reuse these registers
    compiler_expr_pop(compiler, value)
    compiler_expr_pop(compiler, key)
}

/*
Form:
-   unary ::= unary_op expression
    unary_op ::= '-' | 'not' | '#'

Assumptions:
-   The desired unary operator was just consumed.

Guarantees:
-   For arithetic and comparison, `expr` ends up as RK.
-   For len, `expr` ends up as a register.
 */
@(private="file")
unary :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    type := parser.consumed.type

    // Compile the operand. We know the first token of the operand is in the lookahead.
    expr^ = parse_precedence(parser, compiler, .Unary)

    /*
    Links:
    -   https://www.lua.org/source/5.1/lcode.c.html#luaK_prefix
    -   https://the-ravi-programming-language.readthedocs.io/en/latest/lua-parser.html#state-transitions

    Notes:
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
                compiler_expr_any_reg(compiler, expr)
            }
        } else {
            // If nested (e.g. `-(-x)`) reuse the register we stored `x` in
            compiler_expr_any_reg(compiler, expr)
        }
        // MUST be set to '.Number' in order to try constant folding.
        dummy := &Expr{}
        expr_set_number(dummy, 0)
        compiler_emit_binary(compiler, .Unm, expr, dummy)
    case .Not:
        compiler_emit_not(compiler, expr)
    case .Pound:
        // OpCode.Len CANNOT operate on constants no matter what.
        compiler_expr_any_reg(compiler, expr)
        dummy := &Expr{}
        expr_set_number(dummy, 0)
        compiler_emit_binary(compiler, .Len, expr, dummy)
    case:
        unreachable()
    }
}

/// INFIX EXPRESSIONS


/*
Form:
-   binary     ::= binary_op expression
    binary_op  ::= arith_op | compare_op
    arith_op   ::= '+' | '-' | '*' | '/' | '%' | '^'
    compare_op ::= '==' | '~=' | '<' | '>' | '<=' | '>='

Notes:
-   '..' is not included due to its unique semantics: neither arguments B nor C
    can be RK.
 */
@(private="file")
binary :: proc(parser: ^Parser, compiler: ^Compiler, left: ^Expr) {
    type := parser.consumed.type
    op: OpCode
    #partial switch type {
    // Arithmetic
    case .Plus:             op = .Add
    case .Dash:             op = .Sub
    case .Star:             op = .Mul
    case .Slash:            op = .Div
    case .Percent:          op = .Mod
    case .Caret:            op = .Pow

    // Comparison
    case .Equals_2:         op = .Eq
    case .Tilde_Eq:         op = .Neq
    case .Left_Angle:       op = .Lt
    case .Right_Angle:      op = .Gt
    case .Left_Angle_Eq:    op = .Leq
    case .Right_Angle_Eq:   op = .Geq

    // Misc.
    case: unreachable()
    }

    if USE_CONSTANT_FOLDING {
        if !expr_is_number(left^) do compiler_expr_regconst(compiler, left)
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
    if prec != .Exponent do prec += Precedence(1)

    // Compile the right-hand-side operand, filling in the details for 'right'.
    right := parse_precedence(parser, compiler, prec)

    /*
    Note:
    -   This is effectively the inline implementation of the only relevant line/s
        from 'lcode.c:luaK_posfix()'.

    Links:
    -   https://www.lua.org/source/5.1/lcode.c.html#luaK_posfix
     */
    compiler_emit_binary(compiler, op, left, &right)
}

/*
Form:
-   concat ::= expression ['..' expression]+

Assumptions:
-   `..` was just consumed and the right-hand-side operand is the lookahead.
-   The left-hand-side operand already resides in `left`.

Guarantees:
-   `left` will be pushed to the stack, then it will be of type `.Need_Register`
    where its `pc` field will refer to the location of `OpCode.Concat`.

Notes:
-   Concat is treated as right-associative for optimization via recursive calls.
-   This means that recursive call's `left` parameter will also refer to its
    parent caller's `right` parameter.
-   This is because attempting to optimize within a loop is a lot harder than it
    seems.

Links:
-   http://lua-users.org/wiki/AssociativityOfConcatenation
 */
@(private="file")
concat :: proc(parser: ^Parser, compiler: ^Compiler, left: ^Expr) {
    // Left-hand operand MUST be on the stack
    compiler_expr_next_reg(compiler, left)

    // If recursive concat, this will be `.Need_Register` as well.
    right := parse_precedence(parser, compiler, .Concat)
    compiler_emit_concat(compiler, left, &right)
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
        .Dash       = {prefix = unary,      infix = binary,     prec = .Terminal},
        .Plus       = {                     infix = binary,     prec = .Terminal},
        .Star ..= .Percent = {              infix = binary,     prec = .Factor},
        .Caret      = {                     infix = binary,     prec = .Exponent},

        // Comparison
        .Equals_2 ..= .Tilde_Eq = {         infix = binary,     prec = .Equality},
        .Left_Angle ..= .Right_Angle_Eq = { infix = binary,     prec = .Comparison},

        // Other
        .Ellipsis_2 = {                     infix = concat,     prec = .Concat},
        .Pound      = {prefix = unary},

        // Literals
        .Number     = {prefix = literal},
        .String     = {prefix = literal},
        .Identifier = {prefix = variable},
    }
    return rules[type]
}

