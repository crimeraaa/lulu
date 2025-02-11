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
    using info: Expr_Info,
    type:       Expr_Type,
}

Expr_Info :: struct #raw_union {
    number: f64, // .Number
    reg:    u16, // .Discharged
    index:  u32, // .Constant, .Global
    pc:     int, // .Need_Register
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
    Local,
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
    case .Number:           fmt.sbprintf(&builder, "number = %f", expr.number)
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
    token := lexer_scan_token(&parser.lexer)
    parser.consumed, parser.lookahead = parser.lookahead, token
}

// Analogous to 'compiler.c:consume()' in the book.
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
    // consumed the '<identifier>'. Also, Lua doesn't have a 'var' keyword.
    variable(parser, compiler, &last.variable)

    // Use recursive calls to create a stack-allocated linked list.
    if parser_match(parser, .Comma) {
        next := &LValue{prev = last}
        parser_consume(parser, .Identifier)
        assignment(parser, compiler, next)
        return // Prevent parents of recursive calls from consuming '='
    }
    parser_consume(parser, .Equals)

    count_lvalues := lvalue_count(last)
    count_exprs   := expr_list(parser, compiler)

    if count_exprs > count_lvalues {
        // a, b, c = 1, 2, 3, 4; free_reg = 4; count_lvalues = 3; count_exprs = 4;
        compiler.free_reg -= count_exprs - count_lvalues
    }
    if count_lvalues > count_exprs {
        // a, b, c, d = 1, 2, 3; free_reg = 3; count_lvalues = 4; count_exprs = 3;
        n   := count_lvalues - count_exprs
        reg := compiler.free_reg
        compiler_reserve_reg(compiler, cast(int)n)
        compiler_emit_nil(compiler, reg, n)
    }

    // a, b, c = 1, 2, 3; free_reg = 3; count_exprs = 3; reg = 2
    reg := compiler.free_reg - 1

    // Assign going downwards and free in the correct order so that these
    // registers can be reused.
    iter := last
    for current in lvalue_iterator(&iter) {
        compiler_emit_ABx(compiler, .Set_Global, reg, current.variable.index)
        compiler_pop_reg(compiler, reg)
        reg -= 1
    }
}

lvalue_count :: proc(last: ^LValue) -> (count: u16) {
    iter := last
    for _ in lvalue_iterator(&iter) {
        count += 1
    }
    return count
}

lvalue_iterator :: proc(iter: ^^LValue) -> (current: ^LValue, ok: bool) {
    current = iter^
    if current == nil {
        return nil, false
    }
    iter^ = current.prev
    return current, true
}

/*
Analogous to:
-   `compiler.c:identifierConstant(Token *name)` in the book.
*/
@(private="file")
identifier_constant :: proc(parser: ^Parser, compiler: ^Compiler, token: Token) -> (index: u32) {
    identifier := ostring_new(parser.vm, token.lexeme)
    value      := value_make_string(identifier)
    return compiler_add_constant(compiler, value)
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
    } else if parser_match(parser, .Do) {
        compiler_begin_scope(compiler)
        block(parser, compiler)
        compiler_end_scope(compiler)
    } else if parser_match(parser, .Local) {
        /*
        Form:
        -   'local' <identifier> ['=' <expression>]
            
        Notes:
        -   Due to the differences in Lua and Lox, we cannot combine local variable
            declarations into our `parseVariable()` analog as we do not have a
            catch-all `var` keyword.
        */
        parser_consume(parser, .Identifier)
        declare_variable(parser, compiler)
        
        expr := &Expr{}
        if parser_match(parser, .Equals) {
            expression(parser, compiler, expr)
        } else {
            expr_init(expr, .Nil)
        }
        /*
        Normally, we want to ensure zero stack effect. However, in this case,
        we want the initialization expressions to remain on the stack as they
        will act as the local variables themselves.
        */
    } else {
        error_at(parser, parser.lookahead, "Expected an expression")
    }
}

/* 
Analogous to:
-   `compiler.c:declareVariable()` in the book.
-   `lparser.c:new_localvar(LexState *ls, TString *name, int n)` in Lua 5.1.

Notes:
-   Assumes a token of type `.Identifier` was just consumed.
 */
@(private="file")
declare_variable :: proc(parser: ^Parser, compiler: ^Compiler) {
    index := identifier_constant(parser, compiler, parser.consumed)
    ident := compiler.chunk.constants[index].ostring
    for local, index in compiler.locals[:compiler.count_local] {
        // We hit an initialized local in an outer scope?
        if local.depth != UNINITIALIZED_LOCAL && local.depth < compiler.scope_depth {
            break
        }
        if local.ident == ident {
            parser_error_consumed(parser, "Shadowing of local variable")
        }
    }
    compiler_add_local(compiler, ident)
}

@(private="file")
block :: proc(parser: ^Parser, compiler: ^Compiler) {
    for !parser_check(parser, .End) && !parser_check(parser, .Eof) {
        // Analog to `compiler.c:declaration()` in the book.
        parser_parse(parser, compiler)
    }
    parser_consume(parser, .End)
}

@(private="file")
print_statement :: proc(parser: ^Parser, compiler: ^Compiler) {
    parser_consume(parser, .Left_Paren)
    n_args: u16
    if !parser_check(parser, .Right_Paren) {
        n_args = expr_list(parser, compiler)
    }
    parser_consume(parser, .Right_Paren)
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
parse_precedence :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr, prec: Precedence) {
    parser_recurse_begin(parser)
    defer parser_recurse_end(parser)

    parser_advance(parser)
    prefix := get_rule(parser.consumed.type).prefix
    if prefix == nil {
        parser_error_consumed(parser, "Expected an expression")
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
parser_error_lookahead :: proc(parser: ^Parser, msg: string) -> ! {
    error_at(parser, parser.lookahead, msg)
}

// Analogous to 'compiler.c:error()' in the book.
parser_error_consumed :: proc(parser: ^Parser, msg: string) -> ! {
    error_at(parser, parser.consumed, msg)
}

// Analogous to 'compiler.c:errorAt()' in the book.
@(private="file")
error_at :: proc(parser: ^Parser, token: Token, msg: string) -> ! {
    // .Eof token: don't use lexeme as it'll just be an empty string.
    location := token.lexeme if token.type != .Eof else token_type_strings[.Eof]
    vm_throw(parser.vm, .Compile_Error, parser.lexer.source, token.line, "%s at '%s'", msg, location)
}

/// PREFIX EXPRESSIONS

@(private="file")
grouping :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    expression(parser, compiler, expr)
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
    ident := compiler.chunk.constants[index].ostring
    local, ok := compiler_resolve_local(compiler, ident)
    if ok {
        expr_init(expr, .Local)
    } else {
        expr_init(expr, .Global)
        expr.index = index
    }
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
    -   Otherwise, calls to `compiler_pop_expr()` will push through and mess up
        the free registers counter.
     */
    when USE_CONSTANT_FOLDING {
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
    -   When `!USE_CONSTANT_FOLDING`, this is needed in order to emit the
        arguments in the correct order.
    -   Otherwise, without this, they will be reversed!
     */
    if USE_CONSTANT_FOLDING {
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

    /*
    NOTE(2025-01-19):
    -   This is necessary when both sides are nonconstant binary expressions!
        e.g. `'h' .. 'i' == 'h' .. 'i'`
    -   This is because, by itself, expressions like `concat` result in
        `.Need_Register`.
     */
    compiler_expr_regconst(compiler, expr)
    prec  := get_rule(type).prec
    right := &Expr{}
    parse_precedence(parser, compiler, right, prec + Precedence(1))
    compiler_emit_compare(compiler, op, expr, right)
}

/*
Notes:
-   Assumes we just consumed `'..'` and the first right-hand-side operand is
    the lookahead.
-   Concat is treated as right-associative for optimization via recursive calls.
-   This is because attempting to optimize within a loop is a lot harder than it
    seems.

Links:
-   http://lua-users.org/wiki/AssociativityOfConcatenation
 */
@(private="file")
concat :: proc(parser: ^Parser, compiler: ^Compiler, expr: ^Expr) {
    // Left-hand operand MUST be on the stack
    compiler_expr_next_reg(compiler, expr)

    // If recursive concat, this will be `.Need_Register` as well.
    next := &Expr{}
    parse_precedence(parser, compiler, next, .Concat)
    compiler_emit_concat(compiler, expr, next)
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

