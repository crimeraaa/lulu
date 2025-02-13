/// local
#include "parser.h"
#include "vm.h"
#include "object.h"

/// standard
#include <stdio.h> // printf

/**
 * Because C++ is an annoying little s@%!, we have to explicitly define operations
 * even for basic enum types, even if they're contiguous.
 */
#if defined(__cplusplus)

static Precedence
operator+(Precedence prec, int addend)
{
    int result = cast(int)prec + addend;
    return cast(Precedence)result;
}

// unused 'int' argument indicates this is postfix
static Precedence
operator++(Precedence &prec, int)
{
    prec = operator+(prec, 1);
    return prec;
}

#endif // __cplusplus

static void
expression(Parser *self, Compiler *compiler);

static void
statement(Parser *parser, Compiler *compiler);

static void
parse_precedence(Parser *parser, Compiler *compiler, Precedence precedence);

static Parse_Rule *
get_rule(Token_Type type);

/**
 * @note 2024-12-13
 *      Assumes we just consumed a '=' token.
 */
static u16
parse_expression_list(Parser *parser, Compiler *compiler)
{
    u16 count = 0;
    do {
        expression(parser, compiler);
        count++;
    } while (parser_match_token(parser, TOKEN_COMMA));
    return count;
}

LULU_ATTR_UNUSED
static void
print_token(const Token *token, cstring name)
{
    printf("%s{type=%i, lexeme={\"%.*s\"},line=%i}",
        name,
        cast(int)token->type,
        cast(int)token->len,
        token->start,
        token->line);
}

LULU_ATTR_UNUSED
static void
print_parser(const Parser *parser)
{
    print_token(&parser->consumed, "consumed");
    printf("\t");
    print_token(&parser->lookahead, "lookahead");
    printf("\n");
}

void
parser_init(lulu_VM *vm, Parser *self, Compiler *compiler, cstring filename, const char *input, isize len)
{
    lexer_init(vm, &self->lexer, filename, input, len);
    token_init_empty(&self->consumed);
    token_init_empty(&self->lookahead);
    self->compiler = compiler;
}

void
parser_advance_token(Parser *self)
{
    Lexer *lexer = &self->lexer;
    Token  token = lexer_scan_token(lexer);

    // Should be normally impossible, but just in case
    if (token.type == TOKEN_ERROR) {
        parser_error_current(self, "Unhandled error token");
    }

    self->consumed = self->lookahead;
    self->lookahead  = token;
    // print_parser(parser); //! DEBUG
}

static inline bool
check_token(Parser *parser, Token_Type type)
{
    return parser->lookahead.type == type;
}

void
parser_consume_token(Parser *self, Token_Type type, cstring msg)
{
    if (parser_match_token(self, type)) {
        return;
    }
    char buf[256];
    int  len = snprintf(buf, size_of(buf), "Expected '%s' %s",
        LULU_TOKEN_STRINGS[type].data, msg);
    buf[len] = '\0';
    parser_error_current(self, buf);
}

bool
parser_match_token(Parser *self, Token_Type type)
{
    if (check_token(self, type)) {
        parser_advance_token(self);
        return true;
    }
    return false;
}

LULU_ATTR_NORETURN
static void
wrap_error(lulu_VM *vm, cstring filename, const Token *token, cstring msg)
{
    const char *where = token->start;
    isize       len   = token->len;
    if (token->type == TOKEN_EOF) {
        const LString str = LULU_TOKEN_STRINGS[TOKEN_EOF];
        where = str.data;
        len   = str.len;
    }
    vm_comptime_error(vm, filename, token->line, msg, where, cast(int)len);
}

void
parser_error_current(Parser *self, cstring msg)
{
    Lexer *lexer = &self->lexer;
    wrap_error(lexer->vm, lexer->filename, &self->lookahead, msg);
}

void
parser_error_consumed(Parser *self, cstring msg)
{
    Lexer *lexer = &self->lexer;
    wrap_error(lexer->vm, lexer->filename, &self->consumed, msg);
}

static OpCode
get_binary_op(Token_Type type)
{
    switch (type) {
    case TOKEN_PLUS:            return OP_ADD;
    case TOKEN_DASH:            return OP_SUB;
    case TOKEN_STAR:            return OP_MUL;
    case TOKEN_SLASH:           return OP_DIV;
    case TOKEN_PERCENT:         return OP_MOD;
    case TOKEN_CARET:           return OP_POW;

    case TOKEN_TILDE_EQUAL:
    case TOKEN_EQUAL_EQUAL:     return OP_EQ;
    case TOKEN_ANGLE_R_EQUAL:
    case TOKEN_ANGLE_L:         return OP_LT;
    case TOKEN_ANGLE_R:
    case TOKEN_ANGLE_L_EQUAL:   return OP_LEQ;

    default:
        __builtin_unreachable();
    }
}

// Assumes we just consumed a '..' token.
static void
concat(Parser *parser, Compiler *compiler)
{
    u16 argc = 1;
    for (;;) {
        if (argc >= cast(int)LULU_MAX_BYTE) {
            parser_error_consumed(parser, "Too many consecutive concatenations");
        }
        parse_precedence(parser, compiler, PREC_CONCAT + 1);
        argc++;
        if (!parser_match_token(parser, TOKEN_ELLIPSIS_2)) {
            break;
        }
    }
    compiler_emit_A(compiler, OP_CONCAT, argc);
}

static void
binary(Parser *parser, Compiler *compiler)
{
    Token_Type type = parser->consumed.type;
    Precedence prec = get_rule(type)->precedence;
    // For exponentiation, enforce right associativity.
    if (prec != PREC_POW) {
        prec++;
    }
    parse_precedence(parser, compiler, prec);
    compiler_emit_op(compiler, get_binary_op(type));

    // NOT, GT and GEQ are implemented as complements of EQ, LEQ and LT.
    switch (type) {
    case TOKEN_TILDE_EQUAL:
    case TOKEN_ANGLE_R:
    case TOKEN_ANGLE_R_EQUAL:
        compiler_emit_op(compiler, OP_NOT);
        break;
    default:
        break;
    }

}

static void
literal(Parser *parser, Compiler *compiler)
{
    Lexer *lexer = &parser->lexer;
    switch (parser->consumed.type) {
    case TOKEN_FALSE:
        compiler_emit_op(compiler, OP_FALSE);
        break;
    case TOKEN_NIL:
        compiler_emit_nil(compiler, 1);
        break;
    case TOKEN_TRUE:
        compiler_emit_op(compiler, OP_TRUE);
        break;
    case TOKEN_STRING_LIT: {
        Value tmp;
        value_set_string(&tmp, lexer->string);
        compiler_emit_constant(compiler, &tmp);
        break;
    }
    case TOKEN_NUMBER_LIT: {
        compiler_emit_number(compiler, lexer->number);
        break;
    }
    default:
        __builtin_unreachable();
    }
}

/**
 * @note 2024-12-10
 *      Analogous to `compiler.c:namedVariable()` in the book.
 */
static void
named_variable(Parser *parser, Compiler *compiler, Token *ident)
{
    int local = compiler_resolve_local(compiler, &parser->consumed);
    if (local == UNRESOLVED_LOCAL) {
        u32 global = compiler_identifier_constant(compiler, ident);
        compiler_emit_ABC(compiler, OP_GET_GLOBAL, global);
    } else {
        compiler_emit_A(compiler, OP_GET_LOCAL, cast(u16)local);
    }

    for (;;) {
        if (parser_match_token(parser, TOKEN_PERIOD)) {
            parser_consume_token(parser, TOKEN_IDENTIFIER, "after '.'");
            compiler_emit_string(compiler, &parser->consumed);
            compiler_emit_op(compiler, OP_GET_TABLE);
        } else if (parser_match_token(parser, TOKEN_BRACKET_L)) {
            expression(parser, compiler);
            compiler_emit_op(compiler, OP_GET_TABLE);
            parser_consume_token(parser, TOKEN_BRACKET_R, "to close '['");
        } else {
            break;
        }
    }
}

static void
identifier(Parser *parser, Compiler *compiler)
{
    named_variable(parser, compiler, &parser->consumed);
}

/**
 * @brief
 *      Recursively compiles any and all nested expressions in order to emit the
 *      bytecode to evaluate them first.
 *
 * @note 2024-09-07
 *      Assumes we just consumed a ')' character.
 */
static void
grouping(Parser *parser, Compiler *compiler)
{
    expression(parser, compiler);
    parser_consume_token(parser, TOKEN_PAREN_R, "after expression");
}

static OpCode
get_unary_opcode(Token_Type type)
{
    switch (type) {
    case TOKEN_POUND: return OP_LEN;
    case TOKEN_DASH:  return OP_UNM;
    case TOKEN_NOT:   return OP_NOT;
    default:
        __builtin_unreachable();
    }
}

/**
 * @note 2024-09-07
 *      Assumes we just consumed some unary operator, like '-' or '#'.
 */
static void
unary(Parser *parser, Compiler *compiler)
{
    // Saved in stack frame memory as recursion will update parser->consumed.
    Token_Type type = parser->consumed.type;

    // Compile the operand.
    parse_precedence(parser, compiler, PREC_UNARY);
    compiler_emit_op(compiler, get_unary_opcode(type));
}

static inline void
parser_unscan_token(Parser *parser, const Token *prev_consumed)
{
    lexer_unscan_token(&parser->lexer, &parser->lookahead);
    parser->lookahead = parser->consumed;
    parser->consumed  = *prev_consumed;
}

/**
 * @note 2024-12-10
 *      Assumes we just consumed a '{'.
 */
static void
table(Parser *parser, Compiler *compiler)
{
    int i_code  = compiler_new_table(compiler);
    u16 i_table = cast(u16)(compiler->stack_usage - 1);
    u16 n_hash  = 0;
    u16 n_array = 0;
    // Have 1 or more fields to deal with?
    while (!check_token(parser, TOKEN_CURLY_R)) {
        u16   i_key = cast(u16)compiler->stack_usage;
        Token prev  = parser->consumed;
        if (parser_match_token(parser, TOKEN_BRACKET_L)) {
            expression(parser, compiler);
            parser_consume_token(parser, TOKEN_BRACKET_R, "to close '['");
            parser_consume_token(parser, TOKEN_EQUAL, "in field assignment");
            goto field_value;
        } else if (parser_match_token(parser, TOKEN_IDENTIFIER)) {
            // Assigning a named field?
            Token ident = parser->consumed;
            if (parser_match_token(parser, TOKEN_EQUAL)) {
                compiler_emit_string(compiler, &ident);

            field_value:
                n_hash += 1;
                expression(parser, compiler);
                compiler_set_table(compiler, i_table, i_key, 2);
            } else {
                // Reset token stream so we can consume the full expression properly.
                parser_unscan_token(parser, &prev);
                goto indexed;
            }
        } else indexed: {
            n_array += 1;
            expression(parser, compiler);
        }
        if (!parser_match_token(parser, TOKEN_COMMA)) {
            break;
        }
    }

    compiler_adjust_table(compiler, i_code, i_table, n_hash, n_array);
    // print_parser(parser);
    parser_consume_token(parser, TOKEN_CURLY_R, "to close table constructor");
}

///=== PARSER RULES ========================================================={{{

// @todo 2024-09-22 Just remove the designated initializers entirely!
#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wc99-designator"
#elif defined _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 7555)
#endif

static Parse_Rule
LULU_PARSE_RULES[] = {

///=== RESERVED WORDS ==========================================================

// key                  :  prefix_fn    infix_fn    precedence
[TOKEN_AND]             = {NULL,        NULL,       PREC_NONE},
[TOKEN_BREAK]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_DO]              = {NULL,        NULL,       PREC_NONE},
[TOKEN_ELSE]            = {NULL,        NULL,       PREC_NONE},
[TOKEN_ELSEIF]          = {NULL,        NULL,       PREC_NONE},
[TOKEN_END]             = {NULL,        NULL,       PREC_NONE},
[TOKEN_FALSE]           = {&literal,    NULL,       PREC_NONE},
[TOKEN_FOR]             = {NULL,        NULL,       PREC_NONE},
[TOKEN_FUNCTION]        = {NULL,        NULL,       PREC_NONE},
[TOKEN_IF]              = {NULL,        NULL,       PREC_NONE},
[TOKEN_IN]              = {NULL,        NULL,       PREC_NONE},
[TOKEN_LOCAL]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_NIL]             = {&literal,    NULL,       PREC_NONE},
[TOKEN_NOT]             = {&unary,      NULL,       PREC_NONE},
[TOKEN_OR]              = {NULL,        NULL,       PREC_NONE},
[TOKEN_PRINT]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_REPEAT]          = {NULL,        NULL,       PREC_NONE},
[TOKEN_RETURN]          = {NULL,        NULL,       PREC_NONE},
[TOKEN_THEN]            = {NULL,        NULL,       PREC_NONE},
[TOKEN_TRUE]            = {&literal,    NULL,       PREC_NONE},
[TOKEN_UNTIL]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_WHILE]           = {NULL,        NULL,       PREC_NONE},

///=============================================================================

///=== SINGLE=CHARACTER TOKENS =================================================

// key                  :  prefix_fn    infix_fn    precedence
[TOKEN_PAREN_L]         = {&grouping,   NULL,       PREC_NONE},
[TOKEN_PAREN_R]         = {NULL,        NULL,       PREC_NONE},
[TOKEN_BRACKET_L]       = {NULL,        NULL,       PREC_NONE},
[TOKEN_BRACKET_R]       = {NULL,        NULL,       PREC_NONE},
[TOKEN_CURLY_L]         = {&table,      NULL,       PREC_NONE},
[TOKEN_CURLY_R]         = {NULL,        NULL,       PREC_NONE},
[TOKEN_COMMA]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_COLON]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_SEMICOLON]       = {NULL,        NULL,       PREC_NONE},
[TOKEN_ELLIPSIS_3]      = {NULL,        NULL,       PREC_NONE},
[TOKEN_ELLIPSIS_2]      = {NULL,        &concat,    PREC_CONCAT},
[TOKEN_PERIOD]          = {NULL,        NULL,       PREC_NONE},
[TOKEN_POUND]           = {&unary,      NULL,       PREC_NONE},
[TOKEN_PLUS]            = {NULL,        &binary,    PREC_TERM},
[TOKEN_DASH]            = {&unary,      &binary,    PREC_TERM},
[TOKEN_STAR]            = {NULL,        &binary,    PREC_FACTOR},
[TOKEN_SLASH]           = {NULL,        &binary,    PREC_FACTOR},
[TOKEN_PERCENT]         = {NULL,        &binary,    PREC_FACTOR},
[TOKEN_CARET]           = {NULL,        &binary,    PREC_POW},

///=============================================================================

// key                  :  prefix_fn    infix_fn    precedence
[TOKEN_EQUAL]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_EQUAL_EQUAL]     = {NULL,        &binary,    PREC_EQUALITY},
[TOKEN_TILDE_EQUAL]     = {NULL,        &binary,    PREC_COMPARISON},
[TOKEN_ANGLE_L]         = {NULL,        &binary,    PREC_COMPARISON},
[TOKEN_ANGLE_L_EQUAL]   = {NULL,        &binary,    PREC_COMPARISON},
[TOKEN_ANGLE_R]         = {NULL,        &binary,    PREC_COMPARISON},
[TOKEN_ANGLE_R_EQUAL]   = {NULL,        &binary,    PREC_COMPARISON},
[TOKEN_IDENTIFIER]      = {&identifier, NULL,       PREC_NONE},
[TOKEN_STRING_LIT]      = {&literal,    NULL,       PREC_NONE},
[TOKEN_NUMBER_LIT]      = {&literal,    NULL,       PREC_NONE},
[TOKEN_ERROR]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_EOF]             = {NULL,        NULL,       PREC_NONE},

};

#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#elif defined _MSC_VER
    #pragma warning(pop)
#endif

/// }}}=========================================================================

static void
expression(Parser *parser, Compiler *compiler)
{
    parse_precedence(parser, compiler, PREC_ASSIGNMENT + 1);
}

static void
block(Parser *parser, Compiler *compiler)
{
    while (!check_token(parser, TOKEN_END) && !check_token(parser, TOKEN_EOF)) {
        parser_declaration(parser, compiler);
    }
    parser_consume_token(parser, TOKEN_END, "to close 'do' block");
}

static void
print_statement(Parser *parser, Compiler *compiler)
{
    u16 argc = 0;
    parser_consume_token(parser, TOKEN_PAREN_L, "after 'print'");
    // Potentially have at least 1 argument?
    if (!parser_match_token(parser, TOKEN_PAREN_R)) {
        argc = parse_expression_list(parser, compiler);
        parser_consume_token(parser, TOKEN_PAREN_R, "to close call");
    }
    compiler_emit_A(compiler, OP_PRINT, cast(u16)argc);
}

static void
if_statement(Parser *parser, Compiler *compiler)
{
    expression(parser, compiler);
    parser_consume_token(parser, TOKEN_THEN, "after 'if' condition");
}

static void
statement(Parser *parser, Compiler *compiler)
{
    if (parser_match_token(parser, TOKEN_PRINT)) {
        print_statement(parser, compiler);
    } else if (parser_match_token(parser, TOKEN_IF)) {
        if_statement(parser, compiler);
    } else if (parser_match_token(parser, TOKEN_DO)) {
        compiler_begin_scope(compiler);
        block(parser, compiler);
        compiler_end_scope(compiler);
    } else {
        parser_error_current(parser, "Expected an expression");
    }
    parser_match_token(parser, TOKEN_SEMICOLON);
}

static int
count_assignment_targets(LValue *last)
{
    LValue *iter  = last;
    int     count = 0;
    while (iter) {
        iter = iter->prev;
        count++;
    }
    return count;
}

/**
 * @details assigns > exprs: a, b, c = 1, 2
 * @details assigns < exprs: a, b    = 1, 2, 3
 */
static void
adjust_assignment_list(Compiler *compiler, int assigns, int exprs)
{
    if (assigns > exprs) {
        compiler_emit_nil(compiler, cast(u16)(assigns - exprs));
    } else if (assigns < exprs) {
        compiler_emit_pop(compiler, cast(u16)(exprs - assigns));
    }
}

/**
 * @note 2024-12-10
 *      (Somewhat) Analogous to the book's `compiler.c:defineVariable()`.
 */
static void
assign_lvalues(Parser *parser, Compiler *compiler, LValue *last)
{
    const int assigns  = count_assignment_targets(last);
    const int exprs    = parse_expression_list(parser, compiler);

    adjust_assignment_list(compiler, assigns, exprs);
    compiler_emit_lvalues(compiler, last);
    parser_match_token(parser, TOKEN_SEMICOLON);
}

static bool
resolve_lvalue_field(Parser *parser, Compiler *compiler, LValue *lvalue, u16 i_table)
{
    if (parser_match_token(parser, TOKEN_PERIOD)) {
        parser_consume_token(parser, TOKEN_IDENTIFIER, "after '.'");
        compiler_emit_lvalue_parent(compiler, lvalue);
        compiler_emit_string(compiler, &parser->consumed);
    } else if (parser_match_token(parser, TOKEN_BRACKET_L)) {
        compiler_emit_lvalue_parent(compiler, lvalue);
        expression(parser, compiler);
        parser_consume_token(parser, TOKEN_BRACKET_R, "to close '['");
    } else {
        return false;
    }
    lvalue->type    = LVALUE_TABLE;
    lvalue->i_table = i_table;
    lvalue->i_key   = cast(u16)(compiler->stack_usage - 1);
    lvalue->n_pop   = 1; // Pop value only. We'll clean up later.
    return true;
}

/**
 * @note 2024-10-05
 *      Analogous to 'compiler.c:varDeclaration()' in the book.
 *
 * @note 2024-12-10
 *      Assumes we just consumed an identifier. We add it to the constants table
 *      hereh, so we have no equivalent of `compiler.c:parseVariable()`.
 */
static void
assignment(Parser *parser, Compiler *compiler, LValue *last)
{
    const int local = compiler_resolve_local(compiler, &parser->consumed);

    LValue next;
    next.prev = last;
    // parser->lvalues = &last; // Should end at the deepest recursive call.
    if (local == UNRESOLVED_LOCAL) {
        next.type   = LVALUE_GLOBAL;
        next.global = compiler_identifier_constant(compiler, &parser->consumed);
    } else {
        next.type   = LVALUE_LOCAL;
        next.local  = cast(byte)local;
    }

    // Must be consistent. Concept check: `t.k.v = 10`
    const u16 i_table = cast(u16)compiler->stack_usage;
    while (resolve_lvalue_field(parser, compiler, &next, i_table));


    /**
     * If we have a multiple assignment, resolve all the assignment targets and
     * store them in a linked list which is 'allocated' using recursive stack
     * frames.
     */
    if (parser_match_token(parser, TOKEN_COMMA)) {
        parser_consume_token(parser, TOKEN_IDENTIFIER, "after ','");
        assignment(parser, compiler, &next);
        return; // Prevent recursive calls from consuming `TOKEN_EQUAL`.
    }
    parser_consume_token(parser, TOKEN_EQUAL, "in assignment");
    assign_lvalues(parser, compiler, &next);
}

void
parser_declaration(Parser *self, Compiler *compiler)
{
    if (parser_match_token(self, TOKEN_LOCAL)) {
        int assigns = 0;
        int exprs   = 0;

        do {
            parser_consume_token(self, TOKEN_IDENTIFIER, "after 'local'");
            compiler_add_local(compiler, &self->consumed);
            assigns++;
        } while (parser_match_token(self, TOKEN_COMMA));

        if (parser_match_token(self, TOKEN_EQUAL)) {
            exprs = parse_expression_list(self, compiler);
        }

        adjust_assignment_list(compiler, assigns, exprs);
        compiler_initialize_locals(self->compiler);
        parser_match_token(self, TOKEN_SEMICOLON);
    } else if (parser_match_token(self, TOKEN_IDENTIFIER)) {
        assignment(self, compiler, NULL);
    } else {
        statement(self, compiler);
    }
}

static void
parse_infix(Parser *parser, Compiler *compiler, Precedence precedence)
{
    while (precedence <= get_rule(parser->lookahead.type)->precedence) {
        parser_advance_token(parser);
        get_rule(parser->consumed.type)->infix_fn(parser, compiler);
    }
}

static void
parse_precedence(Parser *parser, Compiler *compiler, Precedence precedence)
{
    parser_advance_token(parser);
    Parse_Fn prefix_rule = get_rule(parser->consumed.type)->prefix_fn;
    if (!prefix_rule) {
        parser_error_consumed(parser, "Expected prefix expression");
        return;
    }
    prefix_rule(parser, compiler);
    parse_infix(parser, compiler, precedence);

    // Don't consume as we want to report the left hand side of the '='.
    if (check_token(parser, TOKEN_EQUAL)) {
        parser_error_consumed(parser, "Invalid assignment target");
    }
}

static Parse_Rule *
get_rule(Token_Type type)
{
    return &LULU_PARSE_RULES[type];
}
