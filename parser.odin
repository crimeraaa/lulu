#+private
package lulu

import "core:fmt"

Parser :: struct {
    lexer: Lexer,
    consumed, lookahead: Token,
    panicking: bool,
}

Parse_Rule :: struct {
    prefix, infix:  #type proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr),
    prec:           Precedence,
}

Precedence :: enum u8 {
    None,
    Assignment, // =
    Or,         // or
    And,        // and
    Equality,   // == ~=
    Comparison, // < > <= >=
    Terminal,   // + -
    Factor,     // * / %
    Exponent,   // ^
    Unary,      // - # not
    Call,       // . ()
    Primary,
}

Expr :: struct {
    prev:   ^Expr,
    value:  Expr_Value,
    data:   u32,    // Index of literal in constants table.
    type:   Expr_Type,
}

Expr_Value :: union {
    f64,
}

Expr_Type :: enum u8 {
    Literal,
}

// Analogous to 'compiler.c:advance()' in the book.
parser_advance :: proc(parser: ^Parser) {
    token, error := lexer_scan_token(&parser.lexer)
    if error != nil {
        error_at(parser, token, lexer_error_strings[error])
        return
    }
    parser.consumed, parser.lookahead = parser.lookahead, token
}

// Analogous to 'compiler.c:consume()' in the book.
parser_consume :: proc(parser: ^Parser, expected: Token_Type) {
    if parser.lookahead.type == expected {
        parser_advance(parser)
        return
    }
    // @warning 2025-01-05: We assume this is enough!
    buf: [64]byte
    fmt.bprintf(buf[:], "Expected '%s'", token_type_strings[expected])
    parser_error_lookahead(parser, string(buf[:]))
}

// Analogous to 'compiler.c:expression()' in the book.
parser_parse_expression :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    parse_precedence(parser, compiler, expr, .Assignment + Precedence(1))
}

@(private="file")
parse_precedence :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr, prec: Precedence) {
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
    if parser.panicking {
        return
    }
    parser.panicking = true

    location := token.lexeme if token.type != .Eof else token_type_strings[.Eof]
    fmt.eprintfln("%s:%i: %s at '%s'", parser.lexer.source, token.line, msg, location)
}

/// PREFIX EXPRESSIONS

@(private="file")
grouping :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    parser_parse_expression(parser, compiler, expr)
    parser_consume(parser, .Right_Paren)
}

@(private="file")
number :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    n := parser.lexer.number
    expr.type  = .Literal
    expr.value = n
    expr.data  = compiler_add_constant(compiler, n)
}

@(private="file")
unary :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    type := parser.consumed.type

    // Compile the operand. We know the first token of the operand is in the lookahead.
    parse_precedence(parser, compiler, expr, .Unary)

    op: OpCode
    #partial switch type {
    case .Dash: op = .Unm
    case:       unreachable()
    }
}

/// INFIX EXPRESSIONS

@(private="file")
binary :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    type := parser.consumed.type
    rule := get_rule(type)
    next := &Expr{prev = expr}
    parse_precedence(parser, compiler, next, rule.prec + Precedence(1))

    op: OpCode
    #partial switch type {
    case .Plus:     op = .Add
    case .Dash:     op = .Sub
    case .Star:     op = .Mul
    case .Slash:    op = .Div
    case .Percent:  op = .Mod
    case .Caret:    op = .Pow
    case:
        unreachable()
    }
}

/// PRATT PARSER

get_rule :: proc(type: Token_Type) -> (rule: Parse_Rule) {
    @(static, rodata)
    rules := #partial [Token_Type]Parse_Rule {
        .Left_Paren = {prefix = grouping},
        .Dash       = {prefix = unary,      infix = binary,     prec = .Terminal},
        .Plus       = {                     infix = binary,     prec = .Terminal},
        .Star       = {                     infix = binary,     prec = .Factor},
        .Slash      = {                     infix = binary,     prec = .Factor},
        .Percent    = {                     infix = binary,     prec = .Factor},
        .Caret      = {                     infix = binary,     prec = .Exponent},
        .Number     = {prefix = number},
    }
    return rules[type]
}

