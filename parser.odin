#+private
package lulu

import "core:fmt"
import "core:log"
import "core:strings"

Parser :: struct {
    vm                  : ^VM,
    lexer               : Lexer,
    consumed, lookahead : Token,
    panicking           : bool,
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
    info    : Expr_Info,
    type    : Expr_Type,
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
    #partial switch expr.type {
    case .Nil:              break
    case .True:             break
    case .False:            break
    case .Number:           fmt.sbprintf(&builder, "%f", expr.info.number)
    case .Need_Register:    fmt.sbprintf(&builder, "pc = %i", expr.info.pc)
    case .Discharged:       fmt.sbprintf(&builder, "reg = %i", expr.info.reg)
    case .Constant:         fmt.sbprintf(&builder, "index = %i", expr.info.index)
    case:                   unreachable()
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

parser_match :: proc(parser: ^Parser, expected: Token_Type) -> (found: bool) {
    found = parser.lookahead.type == expected
    if found {
        return parser_advance(parser)
    }
    return found
}

parser_check :: proc(parser: ^Parser, expected: Token_Type) -> (found: bool) {
    found = parser.lookahead.type == expected
    return found
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
        assignment(parser, compiler)
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
assignment :: proc(parser: ^Parser, compiler: ^Compiler) {
    // Inline implementation of `compiler.c:parseVariable()` since we immediately
    // consumed the '<identifier>'. Lua doesn't have a 'var' keyword.
    index := identifier_constant(parser, compiler, parser.consumed)
    parser_consume(parser, .Equals)

    expr := &Expr{}
    if !expression(parser, compiler, expr) { return }
    reg := compiler_expr_any_reg(compiler, expr)
    compiler_emit_ABx(compiler, .Set_Global, reg, index)
    compiler_free_expr(compiler, expr)
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

    n_args := cast(u16)0
    if !parser_check(parser, .Right_Paren) {
        // Push arguments, one at a time, on the stack.
        for {
            arg := &Expr{}
            if !expression(parser, compiler, arg) {
                return
            }
            compiler_expr_next_reg(compiler, arg)
            n_args += 1
            parser_match(parser, .Comma) or_break
        }
    }
    if !parser_consume(parser, .Right_Paren) {
        return
    }
    compiler_emit_AB(compiler, .Print, compiler.free_reg - n_args, compiler.free_reg)
    // This is hacky but it works to allow recycling of registers
    compiler.free_reg -= n_args
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
expression :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) -> (ok: bool) {
    return parse_precedence(parser, compiler, expr, .Assignment + Precedence(1))
}

// Analogous to 'lparser.c:subexpr(LexState *ls, expdesc *v, int limit)' in Lua 5.1.
// See: https://www.lua.org/source/5.1/lparser.c.html#subexpr
@(require_results)
parse_precedence :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr, prec: Precedence, location := #caller_location) -> (ok: bool) {
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
        expr.type       = .Constant
        expr.info.index = compiler_add_constant(compiler, value_make_string(parser.lexer.str))
    case:           unreachable()
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
    expr.info.index = index
}

@(private="file")
unary :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    type := parser.consumed.type

    // Compile the operand. We know the first token of the operand is in the lookahead.
    if !parse_precedence(parser, compiler, expr, .Unary) {
        return
    }

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
        dummy := &Expr{}
        expr_set_number(dummy, 0)
        compiler_emit_arith(compiler, .Unm, expr, dummy)
    case .Not:  compiler_emit_not(compiler, expr)
    case: unreachable()
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

    parser_recurse_begin(parser)
    prec := get_rule(type).prec

    // By not adding 1 for exponentiation we enforce right-associativity since we
    // keep emitting the ones to the right first
    if prec != .Exponent {
        prec += Precedence(1)
    }

    // Compile the right-hand-side operand, filling in the details for 'right'.
    right := &Expr{}
    if !parse_precedence(parser, compiler, right, prec) {
        return
    }
    parser_recurse_end(parser)

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
    prec := get_rule(type).prec
    right := &Expr{}
    if !parse_precedence(parser, compiler, right, prec + Precedence(1)) {
        return
    }
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
        if !parse_precedence(parser, compiler, next, .Concat) {
            return
        }
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

