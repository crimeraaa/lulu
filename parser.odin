#+private
package lulu

import "core:fmt"
import "core:log"
import "core:encoding/ansi"

Parser :: struct {
    lexer:               Lexer,
    consumed, lookahead: Token,
    panicking:           bool,
    recurse:             int,
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
    value:  Value,  // Number literal.
    info:   int,    // May refer to a register or constants table index.
    type:   Expr_Type,
}

Expr_Type :: enum u8 {
    Discharged, // This ^Expr was emitted. It is now conceptually read-only.
    Number,     // A number literal.
}

// Intended to be easier to grep
// Inspired by: https://www.lua.org/source/5.1/lparser.c.html#simpleexp
expr_init :: proc(expr: ^Expr, type: Expr_Type, info: int) {
    expr.type = type
    expr.info = info
}

// NOTE: In the future, may need to check for jump lists!
// See: https://www.lua.org/source/5.1/lcode.c.html#isnumeral
expr_is_number :: proc(expr: ^Expr) -> bool {
    return expr.type == .Number
}

// Analogous to 'compiler.c:advance()' in the book.
parser_advance :: proc(parser: ^Parser) -> (ok: bool) {
    token, error := lexer_scan_token(&parser.lexer)
    if ok = (error == nil) || (error == .Eof); !ok {
        log.errorf("Error: %v", error)
        error_at(parser, token, lexer_error_strings[error])
    } else {
        parser.consumed, parser.lookahead = parser.lookahead, token
    }
    return ok
}

// Analogous to 'compiler.c:consume()' in the book.
parser_consume :: proc(parser: ^Parser, expected: Token_Type) -> (ok: bool) {
    if parser.lookahead.type == expected {
        return parser_advance(parser)
    }
    // @warning 2025-01-05: We assume this is enough!
    buf: [64]byte
    s := fmt.bprintf(buf[:], "Expected '%s'", token_type_strings[expected])
    parser_error_lookahead(parser, s)
    return false
}

// Analogous to 'compiler.c:expression()' in the book.
parser_parse_expression :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) -> (ok: bool) {
    return parse_precedence(parser, compiler, expr, .Assignment + Precedence(1))
}

@(private="file")
parse_precedence :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr, prec: Precedence, location := #caller_location) -> (ok: bool) {
    recurse_begin(parser, location)
    defer recurse_end(parser, location)
    parser_advance(parser) or_return
    prefix := get_rule(parser.consumed.type).prefix
    if prefix == nil {
        parser_error_consumed(parser, "Expected an expression")
        return false
    }
    // log.debugf("Calling prefix: %v", prefix)
    prefix(parser, compiler, expr)

    for {
        rule := get_rule(parser.lookahead.type)
        if prec > rule.prec {
            break
        }
        parser_advance(parser) or_return
        // log.debugf("Calling infix: %v", rule.infix)
        rule.infix(parser, compiler, expr)
    }

    return true
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
    // Avoid cascading error messages (looking at you, C++)...
    if parser.panicking {
        return
    }
    parser.panicking = true

    // .Eof token: don't use lexeme as it'll just be an empty string.
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
recurse_begin :: proc(parser: ^Parser, location := #caller_location) {
    defer parser.recurse += 1
    if parser.recurse == 0 { return }
    // log.debugf(purple("BEGIN RECURSE(%i)"), parser.recurse, location = location)
}

@(private="file")
recurse_end :: proc(parser: ^Parser, location := #caller_location) {
    parser.recurse -= 1
    if parser.recurse == 0 { return }
    // log.debugf(purple("END RECURSE(%i)"), parser.recurse, location = location)
}

@(private="file")
purple :: proc($msg: string) -> string {
    return set_color(msg, ansi.FG_MAGENTA)
}

@(private="file")
set_color :: proc($msg, $color: string) -> string {
    return ansi.CSI + color + ansi.SGR + msg + ansi.CSI + ansi.RESET + ansi.SGR
}

@(private="file")
number :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    // defer log.debug("expr:", expr)
    expr_init(expr, .Number, 0)
    expr.value = parser.lexer.number
}

@(private="file")
unary :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    get_op :: proc(type: Token_Type) -> (op: OpCode) {
        #partial switch type {
        case .Dash: return .Unm
        case:       log.panicf("Got type: %v", type)
        }
    }
    type := parser.consumed.type
    op   := get_op(type)

    // Compile the operand. We know the first token of the operand is in the lookahead.
    parse_precedence(parser, compiler, expr, .Unary)
    compiler_emit_prefix(compiler, op, expr)
}

/// INFIX EXPRESSIONS

@(private="file")
binary :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    get_op :: proc(type: Token_Type) -> (op: OpCode) {
        #partial switch type {
        case .Plus:     return .Add
        case .Dash:     return .Sub
        case .Star:     return .Mul
        case .Slash:    return .Div
        case .Percent:  return .Mod
        case .Caret:    return .Pow
        case:           log.panicf("Got type: %v", type)
        }
    }
    type  := parser.consumed.type
    right := &Expr{}
    op    := get_op(type)
    // log.debug("left: ", expr)

    // Notice how we call this before filling in 'right'
    compiler_emit_infix(compiler, op, expr)

    // Compile the right-hand-side operand, filling in the details for 'right'.
    recurse_begin(parser)
    parse_precedence(parser, compiler, right, get_rule(type).prec + Precedence(1))
    recurse_end(parser)

    // log.debug("left: ",  expr)
    // log.debug("right:", right)

    compiler_emit_postfix(compiler, op, expr, right)
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

