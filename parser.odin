#+private
package lulu

import "core:c/libc"
import "core:fmt"
import "core:log"
import "core:strings"

Parser :: struct {
    vm                  : ^VM,
    lexer               : Lexer,
    consumed, lookahead : Token,
    recurse             : int,
}

Parse_Rule :: struct {
    prefix, infix   : proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr),
    prec            : Precedence,
}

/*
Links:
-   https://www.lua.org/pil/3.5.html
-   https://www.lua.org/manual/5.1/manual.html#2.5.6
 */
Precedence :: enum u8 {
    None,
    Assignment, // =
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

Expr :: struct {
    using info  : Expr_Info,
    type        : Expr_Type,
}

Expr_Info :: struct #raw_union {
    number  : f64, // .Number
    reg     : u16, // .Discharged
    index   : u32, // .Constant, .Global
    pc      : int, // .Need_Register
}

/*
Links:
-   https://the-ravi-programming-language.readthedocs.io/en/latest/lua-parser.html#state-transitions
 */
Expr_Type :: enum u8 {
    Empty,          // Only used as zero-value. Similar to `VVOID`.
    Discharged,     // This ^Expr was emitted to a register. Similar to `VNONRELOC`.
    Need_Register,  // This ^Expr needs to be assigned to a register. Similar to `VRELOCABLE`.
    Nil,
    True,
    False,
    Number,
    Constant,
    Global,
}

// Intended to be easier to grep
// Inspired by: https://www.lua.org/source/5.1/lparser.c.html#simpleexp
expr_init :: proc(expr: ^Expr, type: Expr_Type) {
    expr.type = type
}

expr_set_number :: proc(expr: ^Expr, n: f64) {
    expr.type   = .Number
    expr.number = n
}

// NOTE: In the future, may need to check for jump lists!
// See: https://www.lua.org/source/5.1/lcode.c.html#isnumeral
expr_is_number :: proc(expr: ^Expr) -> bool {
    return expr.type == .Number
}

// The returned string will not last the next call to this!
expr_to_string :: proc(expr: ^Expr) -> string {
    @(static)
    buf: [64]byte

    builder := strings.builder_from_bytes(buf[:])
    fmt.sbprint(&builder, "{info = {")
    #partial switch expr.type {
    case .Nil, .True, .False:
    case .Number:           fmt.sbprintf(&builder, "%f", expr.number)
    case .Need_Register:    fmt.sbprintf(&builder, "pc = %i", expr.pc)
    case .Discharged:       fmt.sbprintf(&builder, "reg = %i", expr.reg)
    case .Constant:         fmt.sbprintf(&builder, "index = %i", expr.index)
    case:                   unreachable()
    }
    fmt.sbprintf(&builder, "}, type = %s}", expr.type)
    return strings.to_string(builder)
}

// Analogous to 'compiler.c:advance()' in the book.
parser_advance :: proc(parser: ^Parser) {
    token, error := lexer_scan_token(&parser.lexer)
    if ok := (error == nil) || (error == .Eof); !ok {
        log.errorf("Error: %v", error)
        error_at(parser, token, lexer_error_strings[error])
    } else {
        parser.consumed, parser.lookahead = parser.lookahead, token
    }
}

// Analogous to 'compiler.c:consume()' in the book.
parser_consume :: proc(parser: ^Parser, expected: Token_Type) -> (ok: bool) {
    if parser.lookahead.type == expected {
        parser_advance(parser)
        return true
    }
    // @warning 2025-01-05: We assume this is enough!
    buf: [64]byte
    s := fmt.bprintf(buf[:], "Expected '%s'", token_type_strings[expected])
    parser_error_lookahead(parser, s)
    return false
}

parser_match :: proc(parser: ^Parser, expected: Token_Type) -> (found: bool) {
    found = parser.lookahead.type == expected
    if found {
        parser_advance(parser)
    }
    return found
}

parser_check :: proc(parser: ^Parser, expected: Token_Type) -> (found: bool) {
    found = parser.lookahead.type == expected
    return found
}

LValue :: struct {
    prev:    ^LValue,
    variable: Expr,
}

/*
Analogous to:
-   `compiler.c:declaration()` in the book.
-   `lparser.c:chunk(LexState *ls)` in Lua 5.1.

Links:
-   https://www.lua.org/source/5.1/lparser.c.html#chunk
 */
parser_parse :: proc(parser: ^Parser, compiler: ^Compiler) {
    if parser_match(parser, .Identifier) {
        assignment(parser, compiler, &LValue{})
    } else {
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
assignment :: proc(parser: ^Parser, compiler: ^Compiler, last: ^LValue) {
    // Inline implementation of `compiler.c:parseVariable()` since we immediately
    // consumed the '<identifier>'. Lua doesn't have a 'var' keyword.
    variable(parser, compiler, &last.variable)
    if parser_match(parser, .Comma) {
        next := &LValue{prev = last}
        if !parser_consume(parser, .Identifier) {
            return
        }
        assignment(parser, compiler, next)
        return // Prevent recursive calls from consuming '='
    }
    parser_consume(parser, .Equals)

    n_lvalues := count_lvalues(last)
    n_exprs := expr_list(parser, compiler)

    if n_exprs > n_lvalues {
        // a, b, c = 1, 2, 3, 4; free_reg = 4; n_lvalues = 3; n_exprs = 4;
        n_extra := n_exprs - n_lvalues
        compiler.free_reg -= n_extra
    } else if n_lvalues > n_exprs {
        // a, b, c, d = 1, 2, 3; free_reg = 3; n_lvalues = 4; n_exprs = 3;
        n_nils := n_lvalues - n_exprs
        compiler_reserve_reg(compiler, n_nils)
        compiler_emit_nil(compiler, compiler.free_reg, n_nils)
    }

    // a, b, c = 1, 2, 3; free_reg = 3; n_exprs = 3; reg = 2
    reg := compiler.free_reg - 1
    // Assign going downwards
    for current := last; current != nil; current = current.prev {
        defer reg -= 1
        compiler_emit_ABx(compiler, .Set_Global, reg, current.variable.index)
        compiler_free_reg(compiler, reg)
    }
}

@(private="file")
count_lvalues :: proc(last: ^LValue) -> (count: u16) {
    for current := last; current != nil; current = current.prev {
        count += 1
    }
    return count
}

/*
Analogous to:
-   `compiler.c:identifierConstant(Token *name)` in the book.
*/
@(private="file")
identifier_constant :: proc(parser: ^Parser, compiler: ^Compiler, token: Token) -> (index: u32) {
    identifier := ostring_new(parser.vm, token.lexeme)
    return compiler_add_constant(compiler, value_make_string(identifier))
}

/*
Analogous to:
-   `compiler.c:statement()` in the book.
-   `lparser.c:statement(LexState *ls)` in Lua 5.1.

Links:
-   https://www.lua.org/source/5.1/lparser.c.html#statement
 */
@(private="file")
statement :: proc(parser: ^Parser, compiler: ^Compiler) {
    // line := parser.lookahead.line
    if parser_match(parser, .Print) {
        print_statement(parser, compiler)
    } else {
        error_at(parser, parser.lookahead, "Expected an expression")
    }
}

@(private="file")
print_statement :: proc(parser: ^Parser, compiler: ^Compiler) {
    if !parser_consume(parser, .Left_Paren) {
        return
    }
    n_args := expr_list(parser, compiler)
    if !parser_consume(parser, .Right_Paren) {
        return
    }
    compiler_emit_AB(compiler, .Print, compiler.free_reg - n_args, compiler.free_reg)
    // This is hacky but it works to allow recycling of registers
    compiler.free_reg -= n_args
}

// Pushes a comma-separated list of expressions onto the stack.
@(private="file")
expr_list :: proc(parser: ^Parser, compiler: ^Compiler) -> (count: u16) {
    for {
        arg := &Expr{}
        expression(parser, compiler, arg)
        count += 1
        compiler_expr_next_reg(compiler, arg)
        if !parser_match(parser, .Comma) {
            break
        }
    }
    return count
}

/*
Analogous to:
-   `compiler.c:expression()` in the book.

Notes:
-   Expressions only ever produce 1 net resulting value, which should reside in `expr`.
-   However, `expr` itself does not reside in a register yet. It is up to you
    to decide how to allocate that.
 */
@(private="file")
expression :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    parse_precedence(parser, compiler, expr, .Assignment + Precedence(1))
}

// Analogous to 'lparser.c:subexpr(LexState *ls, expdesc *v, int limit)' in Lua 5.1.
// See: https://www.lua.org/source/5.1/lparser.c.html#subexpr
parse_precedence :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr, prec: Precedence, location := #caller_location) {
    parser_recurse_begin(parser, location)
    defer parser_recurse_end(parser, location)

    parser_advance(parser)
    prefix := get_rule(parser.consumed.type).prefix
    if prefix == nil {
        parser_error_consumed(parser, "Expected an expression")
        return
    }
    prefix(parser, compiler, expr)

    for {
        rule := get_rule(parser.lookahead.type)
        if prec > rule.prec {
            break
        }
        // Can occur when we hardcode low precedence recursion in high precedence calls
        assert(rule.infix != nil)
        parser_advance(parser)
        rule.infix(parser, compiler, expr)
    }
}

// Analogous to 'compiler.c:errorAtCurrent()' in the book.
parser_error_lookahead :: proc(parser: ^Parser, msg: string) {
    error_at(parser, parser.lookahead, msg)
}

// Analogous to 'compiler.c:error()' in the book.
parser_error_consumed :: proc(parser: ^Parser, msg: string) {
    error_at(parser, parser.consumed, msg)
}

// Analogous to 'compiler.c:errorAt()' in the book.
@(private="file")
error_at :: proc(parser: ^Parser, token: Token, msg: string) {
    // .Eof token: don't use lexeme as it'll just be an empty string.
    location := token.lexeme if token.type != .Eof else token_type_strings[.Eof]
    vm_error(parser.vm, .Compile_Error, parser.lexer.source, token.line, "%s at '%s'", msg, location)
}

/// PREFIX EXPRESSIONS

@(private="file")
grouping :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    expression(parser, compiler, expr)
    parser_consume(parser, .Right_Paren)
}

parser_recurse_begin :: proc(parser: ^Parser, location := #caller_location) {
    parser.recurse += 1
}

parser_recurse_end :: proc(parser: ^Parser, location := #caller_location) {
    parser.recurse -= 1
}

@(private="file")
literal :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    #partial switch type := parser.consumed.type; type {
    case .Nil:      expr_init(expr, .Nil)
    case .True:     expr_init(expr, .True)
    case .False:    expr_init(expr, .False)
    case .Number:   expr_set_number(expr, parser.lexer.number)
    case .String:
        index := compiler_add_constant(compiler, value_make_string(parser.lexer.str))
        expr_init(expr, .Constant)
        expr.index = index
    case: unreachable()
    }
}

/*
Assumptions:
-   `parser.consumed` is of type `.Identifier`.
 */
@(private="file")
variable :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    // Inline implementation of `compiler.c:namedVariable(Token name)` in the book.
    index := identifier_constant(parser, compiler, parser.consumed)
    expr_init(expr, .Global)
    expr.index = index
}

@(private="file")
unary :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    type := parser.consumed.type

    // Compile the operand. We know the first token of the operand is in the lookahead.
    parse_precedence(parser, compiler, expr, .Unary)

    /*
    Note:
    -   Inline implementation of the only relevant lines from `lcode.c:luaK_prefix()`.

    Links:
    -   https://www.lua.org/source/5.1/lcode.c.html#luaK_prefix
    -   https://the-ravi-programming-language.readthedocs.io/en/latest/lua-parser.html#state-transitions

    Notes:
    -   Ensure the zero-value for `Expr_Type` is anything BUT `.Discharged`.
    -   Otherwise, calls to `compiler_free_expr()` will push through and mess up
        the free registers counter.
     */
    when CONSTANT_FOLDING_ENABLED {
        if !(.Nil <= expr.type && expr.type <= .Number) {
            compiler_expr_any_reg(compiler, expr)
        }
    } else {
        compiler_expr_any_reg(compiler, expr)
    }
    #partial switch type {
    case .Dash:
        // MUST be set to '.Number' in order to try constant folding.
        dummy := &Expr{}
        expr_set_number(dummy, 0)
        compiler_emit_arith(compiler, .Unm, expr, dummy)
    case .Not:  compiler_emit_not(compiler, expr)
    case:       unreachable()
    }
}

/// INFIX EXPRESSIONS

@(private="file")
arith :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    type := parser.consumed.type
    op: OpCode
    #partial switch type {
    case .Plus:     op = .Add
    case .Dash:     op = .Sub
    case .Star:     op = .Mul
    case .Slash:    op = .Div
    case .Percent:  op = .Mod
    case .Caret:    op = .Pow
    case: unreachable()
    }

    prec := get_rule(type).prec

    // By not adding 1 for exponentiation we enforce right-associativity since we
    // keep emitting the ones to the right first
    if prec != .Exponent {
        prec += Precedence(1)
    }

    // Compile the right-hand-side operand, filling in the details for 'right'.
    right := &Expr{}
    parse_precedence(parser, compiler, right, prec)


    /*
    Notes:
    -   When `!CONSTANT_FOLDING_ENABLED`, this is needed in order to emit the
        arguments in the correct order.
    -   Otherwise, without this, they will be reversed!
     */
    if CONSTANT_FOLDING_ENABLED {
        if !expr_is_number(expr) {
            compiler_expr_regconst(compiler, expr)
        }
    } else {
        compiler_expr_regconst(compiler, expr)
    }

    /*
    Note:
    -   This is effectively the inline implementation of the only relevant line/s
        from 'lcode.c:luaK_posfix()'.

    Links:
    -   https://www.lua.org/source/5.1/lcode.c.html#luaK_posfix
     */
    compiler_emit_arith(compiler, op, expr, right)
}

@(private="file")
compare :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    type := parser.consumed.type
    op: OpCode
    #partial switch type {
    case .Equals_2:         op = .Eq
    case .Tilde_Eq:         op = .Neq
    case .Left_Angle:       op = .Lt
    case .Right_Angle:      op = .Gt
    case .Left_Angle_Eq:    op = .Leq
    case .Right_Angle_Eq:   op = .Geq
    case:                   unreachable()
    }

    parser_recurse_begin(parser)
    prec  := get_rule(type).prec
    right := &Expr{}
    parse_precedence(parser, compiler, right, prec + Precedence(1))
    parser_recurse_end(parser)
    compiler_emit_compare(compiler, op, expr, right)
}

/*
Notes:
-   Assumes we just consumed `'..'` and the first right-hand-side operand is
    the lookahead.
 */
@(private="file")
concat :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    // First operand MUST be on the stack
    compiler_expr_next_reg(compiler, expr)
    for {
        next := &Expr{}
        parse_precedence(parser, compiler, next, .Concat)
        compiler_emit_concat(compiler, expr, next)
        parser_match(parser, .Ellipsis_2) or_break
    }
}

get_rule :: proc(type: Token_Type) -> (rule: Parse_Rule) {
    @(static, rodata)
    rules := #partial [Token_Type]Parse_Rule {
        .Nil        = {prefix = literal},
        .True       = {prefix = literal},
        .False      = {prefix = literal},
        .Left_Paren = {prefix = grouping},
        .Dash       = {prefix = unary,      infix = arith,     prec = .Terminal},
        .Plus       = {                     infix = arith,     prec = .Terminal},
        .Star       = {                     infix = arith,     prec = .Factor},
        .Slash      = {                     infix = arith,     prec = .Factor},
        .Percent    = {                     infix = arith,     prec = .Factor},
        .Caret      = {                     infix = arith,     prec = .Exponent},
        .Equals_2 ..= .Tilde_Eq = {         infix = compare,   prec = .Equality},
        .Left_Angle ..= .Right_Angle_Eq = { infix = compare,   prec = .Comparison},
        .Ellipsis_2 = {                     infix = concat,    prec = .Concat},
        .Not        = {prefix = unary},
        .Number     = {prefix = literal},
        .String     = {prefix = literal},
        .Identifier = {prefix = variable},
    }
    return rules[type]
}

