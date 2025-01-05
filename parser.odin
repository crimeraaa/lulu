#+private
package lulu

import "core:fmt"
import "core:log"

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
    value:  Value,  // Number literal.
    index:  u32,    // Index into constants table if applicable.
    type:   Expr_Type,
}

Expr_Type :: enum u8 {
    Reusable,   // This ^Expr was previously used but is now available!
    Literal,
}

// Analogous to 'compiler.c:advance()' in the book.
parser_advance :: proc(parser: ^Parser) -> (ok: bool) {
    token, error := lexer_scan_token(&parser.lexer)
    ok = error == nil || error == .Eof
    if !ok {
        log.errorf("Error: %v", error)
        error_at(parser, token, lexer_error_strings[error])
    } else {
        parser.consumed, parser.lookahead = parser.lookahead, token
    }
    return ok
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
    if !parser_advance(parser) {
        return
    }
    prefix := get_rule(parser.consumed.type).prefix
    if prefix == nil {
        parser_error_consumed(parser, "Expected an expression")
        return
    }
    log.debugf("Calling prefix: %v", prefix)
    prefix(parser, compiler, expr)

    for {
        rule := get_rule(parser.lookahead.type)
        if prec > rule.prec {
            break
        }
        if !parser_advance(parser) {
            return
        }
        log.debugf("Calling infix: %v", rule.infix)
        rule.infix(parser, compiler, expr)
    }

    defer log.debug("expr:", expr)

    // Discharge expression
    switch expr.type {
    case .Literal:
        // Not the last literal in the chain if folded?
        if expr.prev != nil {
            break
        }
        defer {
            compiler.free_reg += 1
            expr.type = .Reusable
        }
        a := compiler.free_reg
        index := compiler_add_constant(compiler, expr.value)
        inst  := inst_create_AuBC(.Constant, cast(u16)a, index)
        compiler_emit_instruction(compiler, parser, inst)
    case .Reusable:
        // Nothing to do
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
    log.debug("Recursing...")
    parser_parse_expression(parser, compiler, expr)
    parser_consume(parser, .Right_Paren)
}

@(private="file")
number :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    defer log.debug("expr:", expr)
    expr.type   = .Literal
    expr.value  = parser.lexer.number
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
    left  := expr
    right := &Expr{prev = left}
    op    := get_op(type)

    // Compile the right-hand-side operand, filling in the details for 'right'.
    log.debug("Recursing...")
    parse_precedence(parser, compiler, right, get_rule(type).prec + Precedence(1))
    log.debug("left:",  left)
    log.debug("right:", right)
    // Can do constant folding?
    if (left.type == .Literal) && right.type == .Literal {
        Value_Proc :: #type proc(a, b: Value) -> Value
        dispatch   :: proc(op: OpCode) -> Value_Proc {
            #partial switch op {
            case .Add:  return value_add
            case .Sub:  return value_sub
            case .Mul:  return value_mul
            case .Div:  return value_div
            case .Mod:  return value_mod
            case .Pow:  return value_pow
            case:       log.panicf("Got op: %v", op)
            }
        }

        // 'left' is the same as 'expr', which the caller can sees.
        n := dispatch(op)(left.value, right.value)
        left.value = n
        log.debug("left:", left)
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

