#+private
package lulu

import "core:fmt"
import "core:log"
import "core:encoding/ansi"
import "core:strings"

_ :: ansi

Parser :: struct {
    lexer:               Lexer,
    consumed, lookahead: Token,
    panicking:           bool,
    recurse:             int,
}

Parse_Rule :: struct {
    prefix, infix:  proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr),
    prec:           Precedence,
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
    Terminal,   // + -
    Factor,     // * / %
    Unary,      // - # not
    Exponent,   // ^
    Call,       // . ()
    Primary,
}

Expr :: struct {
    info: Expr_Info,
    type: Expr_Type,
    line: int,
}

Expr_Info :: struct #raw_union {
    number: f64, // .Number
    reg:    u16, // .Discharged
    index:  u32, // .Constant
    pc:     int, // .Need_Register
}

/*
Links:
-   https://the-ravi-programming-language.readthedocs.io/en/latest/lua-parser.html#state-transitions
 */
Expr_Type :: enum u8 {
    Discharged,     // This ^Expr was emitted to a register. Similar to 'VNONRELOC'.
    Need_Register,  // This ^Expr needs to be assigned to a register. Similar to 'VRELOCABLE'.
    Nil,
    True,
    False,
    Number,
    Constant,
}

// Intended to be easier to grep
// Inspired by: https://www.lua.org/source/5.1/lparser.c.html#simpleexp
expr_init :: proc(expr: ^Expr, type: Expr_Type) {
    expr.type        = type
    expr.info.number = 0
}

expr_set_number :: proc(expr: ^Expr, n: f64) {
    expr.type        = .Number
    expr.info.number = n
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
    switch expr.type {
    case .Nil:              break
    case .True:             break
    case .False:            break
    case .Number:           fmt.sbprintf(&builder, "%f", expr.info.number)
    case .Need_Register:    fmt.sbprintf(&builder, "pc = %i", expr.info.pc)
    case .Discharged:       fmt.sbprintf(&builder, "reg = %i", expr.info.reg)
    case .Constant:         fmt.sbprintf(&builder, "index = %i", expr.info.index)
    }
    fmt.sbprintf(&builder, "}, type = %s}", expr.type)
    return strings.to_string(builder)
}

// Analogous to 'compiler.c:advance()' in the book.
@(require_results)
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
    return parser_parse_precedence(parser, compiler, expr, .Assignment + Precedence(1))
}

// Analogous to 'lparser.c:subexpr(LexState *ls, expdesc *v, int limit)' in Lua 5.1.
// See: https://www.lua.org/source/5.1/lparser.c.html#subexpr
@(require_results)
parser_parse_precedence :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr, prec: Precedence, location := #caller_location) -> (ok: bool) {
    parser_recurse_begin(parser, location)
    defer parser_recurse_end(parser, location)

    parser_advance(parser) or_return
    prefix := get_rule(parser.consumed.type).prefix
    if prefix == nil {
        parser_error_consumed(parser, "Expected an expression")
        return false
    }
    prefix(parser, compiler, expr)

    for {
        rule := get_rule(parser.lookahead.type)
        if prec > rule.prec {
            break
        }
        // Can occur when we hardcode low precedence recursion in high precedence calls
        assert(rule.infix != nil)
        parser_advance(parser) or_return
        rule.infix(parser, compiler, expr)
    }

    // Could have been set from one of prefix/infix functions
    return !parser.panicking
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

parser_recurse_begin :: proc(parser: ^Parser, location := #caller_location) {
    defer parser.recurse += 1
    // if parser.recurse == 0 { return }
    // log.debugf(purple("BEGIN RECURSE(%i)"), parser.recurse, location = location)
}

parser_recurse_end :: proc(parser: ^Parser, location := #caller_location) {
    parser.recurse -= 1
    // if parser.recurse == 0 { return }
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
literal :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    #partial switch type := parser.consumed.type; type {
    case .Nil:      expr_init(expr, .Nil)
    case .True:     expr_init(expr, .True)
    case .False:    expr_init(expr, .False)
    case .Number:   expr_set_number(expr, parser.lexer.number)
    case:           unreachable()
    }
}

@(private="file")
unary :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    type := parser.consumed.type

    // Compile the operand. We know the first token of the operand is in the lookahead.
    if !parser_parse_precedence(parser, compiler, expr, .Unary) {
        return
    }

    /*
    Note:
    -   Inline implementation of the only relevant lines from `lcode.c:luaK_prefix()`.

    Links:
    -   https://www.lua.org/source/5.1/lcode.c.html#luaK_prefix
    -   https://the-ravi-programming-language.readthedocs.io/en/latest/lua-parser.html#state-transitions
     */
    #partial switch type {
    case .Dash:
        log.debug(expr_to_string(expr))
        // Push the operand before OpCode.Unm.
        if !expr_is_number(expr) {
            compiler_expr_any_reg(compiler, expr)
        }

        // Since .Discharged is the zero value, change it! Else we call
        // 'compiler_free_expr()' on a non-existent expression.
        dummy := &Expr{}
        expr_set_number(dummy, 0)
        compiler_emit_arith(compiler, .Unm, expr, dummy)
    case: unreachable()
    }
}

/// INFIX EXPRESSIONS

@(private="file")
binary :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    type  := parser.consumed.type
    op: OpCode
    #partial switch type {
    case .Plus:    op = .Add
    case .Dash:    op = .Sub
    case .Star:    op = .Mul
    case .Slash:   op = .Div
    case .Percent: op = .Mod
    case .Caret:   op = .Pow
    case: unreachable()
    }

    /*
    Brief:
    -   Emits the left hand side.
    -   'expr' could be a number literal or the literals nil, true or false.
    -   It will be then be transformed to represent a constant index or a register.

    Note:
    -   This is effectively the inline implementation of the only relevant line
        from 'lcode.c:luaK_infix()'.

    Links:
    -   https://www.lua.org/source/5.1/lcode.c.html#luaK_infix
     */
    compiler_expr_to_regconst(compiler, expr)

    parser_recurse_begin(parser)
    prec := get_rule(type).prec

    // By not adding 1 for exponentiation we enforce right-associativity since we
    // keep emitting the ones to the right first
    if prec != .Exponent {
        prec += Precedence(1)
    }

    // Compile the right-hand-side operand, filling in the details for 'right'.
    right := &Expr{}
    if !parser_parse_precedence(parser, compiler, right, prec) {
        return
    }
    parser_recurse_end(parser)

    /*
    Note:
    -   This is effectively the inline implementation of the only relevant line/s
        from 'lcode.c:luaK_posfix()'.
    -   We will have to adjust this line when comparison operators are added.

    Links:
    -   https://www.lua.org/source/5.1/lcode.c.html#luaK_posfix
     */
    compiler_emit_arith(compiler, op, expr, right)
    return
}

/// PRATT PARSER

get_rule :: proc(type: Token_Type) -> (rule: Parse_Rule) {
    @(static, rodata)
    rules := #partial [Token_Type]Parse_Rule {
        .Nil        = {prefix = literal},
        .True       = {prefix = literal},
        .False      = {prefix = literal},
        .Left_Paren = {prefix = grouping},
        .Dash       = {prefix = unary,      infix = binary,     prec = .Terminal},
        .Plus       = {                     infix = binary,     prec = .Terminal},
        .Star       = {                     infix = binary,     prec = .Factor},
        .Slash      = {                     infix = binary,     prec = .Factor},
        .Percent    = {                     infix = binary,     prec = .Factor},
        .Caret      = {                     infix = binary,     prec = .Exponent},
        .Number     = {prefix = literal},
    }
    return rules[type]
}

