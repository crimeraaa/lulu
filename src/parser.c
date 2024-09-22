#include "parser.h"
#include "vm.h"
#include "object.h"

#include <stdio.h>
#include <stdlib.h>

/**
 * Because C++ is an annoying little s@%!, we have to explicitly define operations
 * even for basic enum types, even if they're contiguous.
 */
#if defined __cplusplus

static lulu_Precedence
operator+(lulu_Precedence prec, int addend)
{
    int result = cast(int)prec + addend;
    return cast(lulu_Precedence)result;
}

static lulu_Precedence
operator++(lulu_Precedence &prec, int)
{
    prec = operator+(prec, 1);
    return prec;
}

#endif // __cplusplus

static void
parse_precedence(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser, lulu_Precedence precedence);

static lulu_Parse_Rule *
get_rule(lulu_Token_Type type);

__attribute__((__unused__))
static void
print_token(const lulu_Token *token, cstring name)
{
    printf("%s{type=%i, lexeme={\"%.*s\"},line=%i}",
        name,
        cast(int)token->type,
        cast(int)token->len,
        token->start,
        token->line);
}

__attribute__((__unused__))
static void
print_parser(const lulu_Parser *self)
{
    print_token(&self->consumed, "consumed");
    printf("\t");
    print_token(&self->current, "current");
    printf("\n");
}

void
lulu_Parse_advance_token(lulu_Lexer *lexer, lulu_Parser *parser)
{
    lulu_Token token = lulu_Lexer_scan_token(lexer);

    // Should be normally impossible, but just in case
    if (token.type == TOKEN_ERROR) {
        lulu_Parse_error_current(lexer, parser, "Unhandled error token");
    }

    parser->consumed = parser->current;
    parser->current  = token;
    // print_parser(parser); //!DEBUG
}

void
lulu_Parse_consume_token(lulu_Lexer *lexer, lulu_Parser *parser, lulu_Token_Type type, cstring msg)
{
    if (parser->current.type == type) {
        lulu_Parse_advance_token(lexer, parser);
        return;
    }
    lulu_Parse_error_current(lexer, parser, msg);
}

#define CSTRING_EOF     "<eof>"

LULU_ATTR_NORETURN static void
wrap_error(lulu_VM *vm, cstring filename, const lulu_Token *token, cstring msg)
{
    const char *where = (token->type == TOKEN_EOF) ? CSTRING_EOF : token->start;
    isize       len   = (token->type == TOKEN_EOF) ? size_of(CSTRING_EOF) - 1 : token->len;
    lulu_VM_comptime_error(vm, filename, token->line, msg, where, len);
}

void
lulu_Parse_error_current(lulu_Lexer *lexer, lulu_Parser *parser, cstring msg)
{
    wrap_error(lexer->vm, lexer->filename, &parser->current, msg);
}

void
lulu_Parse_error_consumed(lulu_Lexer *lexer, lulu_Parser *parser, cstring msg)
{
    wrap_error(lexer->vm, lexer->filename, &parser->consumed, msg);
}

static lulu_OpCode
get_binary_op(lulu_Token_Type type)
{
    switch (type) {
    case TOKEN_PLUS:            return OP_ADD;
    case TOKEN_DASH:            return OP_SUB;
    case TOKEN_ASTERISK:        return OP_MUL;
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

static void
concat(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser)
{
    // Enforce right associativity.
    parse_precedence(compiler, lexer, parser, PREC_CONCAT);
    lulu_Compiler_emit_opcode(compiler, parser, OP_CONCAT);
}

static void
binary(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser)
{
    lulu_Token_Type type = parser->consumed.type;
    lulu_Precedence prec = get_rule(type)->precedence;
    // For exponentiation, enforce right associativity.
    if (prec != PREC_POW) {
        prec++;
    }
    parse_precedence(compiler, lexer, parser, prec);
    lulu_Compiler_emit_opcode(compiler, parser, get_binary_op(type));

    // NOT, GT and GEQ are implemented as complements of EQ, LEQ and LT.
    switch (type) {
    case TOKEN_TILDE_EQUAL:
    case TOKEN_ANGLE_R:
    case TOKEN_ANGLE_R_EQUAL:
        lulu_Compiler_emit_opcode(compiler, parser, OP_NOT);
        break;
    default:
        break;
    }

}

static void
literal(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser)
{
    lulu_Value tmp;
    switch (parser->consumed.type) {
    case TOKEN_FALSE:
        lulu_Compiler_emit_opcode(compiler, parser, OP_FALSE);
        break;
    case TOKEN_NIL:
        lulu_Compiler_emit_byte1(compiler, parser, OP_NIL, 1);
        break;
    case TOKEN_TRUE:
        lulu_Compiler_emit_opcode(compiler, parser, OP_TRUE);
        break;
    case TOKEN_STRING_LIT:
        lulu_Value_set_string(&tmp, lexer->string);
        lulu_Compiler_emit_constant(compiler, lexer, parser, &tmp);
        break;
    case TOKEN_NUMBER_LIT:
        lulu_Value_set_number(&tmp, lexer->number);
        lulu_Compiler_emit_constant(compiler, lexer, parser, &tmp);
        break;
    default:
        __builtin_unreachable();
    }
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
grouping(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser)
{
    lulu_Parse_expression(compiler, lexer, parser);
    lulu_Parse_consume_token(lexer, parser, TOKEN_PAREN_R, "Expected ')' after expression");
}

static lulu_OpCode
get_unary_opcode(lulu_Token_Type type)
{
    switch (type) {
    case TOKEN_DASH: return OP_UNM;
    case TOKEN_NOT:  return OP_NOT;
    default:
        __builtin_unreachable();
    }
}

/**
 * @note 2024-09-07
 *      Assumes we just consumed some unary operator, like '-' or '#'.
 */
static void
unary(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser)
{
    // Saved in stack frame memory as recursion will update `parser->consumed`.
    lulu_Token_Type type = parser->consumed.type;

    // Compile the operand.
    parse_precedence(compiler, lexer, parser, PREC_UNARY);
    lulu_Compiler_emit_opcode(compiler, parser, get_unary_opcode(type));
}

// @todo 2024-09-22 Just remove the designated initializers entirely!
#if defined __GNUC__
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wc99-designator"
#endif

static lulu_Parse_Rule
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
[TOKEN_CURLY_L]         = {NULL,        NULL,       PREC_NONE},
[TOKEN_CURLY_R]         = {NULL,        NULL,       PREC_NONE},
[TOKEN_COMMA]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_COLON]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_SEMICOLON]       = {NULL,        NULL,       PREC_NONE},
[TOKEN_ELLIPSIS_3]      = {NULL,        NULL,       PREC_NONE},
[TOKEN_ELLIPSIS_2]      = {NULL,        &concat,    PREC_CONCAT},
[TOKEN_PERIOD]          = {NULL,        NULL,       PREC_NONE},
[TOKEN_HASH]            = {NULL,        NULL,       PREC_NONE},
[TOKEN_PLUS]            = {NULL,        &binary,    PREC_TERM},
[TOKEN_DASH]            = {&unary,      &binary,    PREC_TERM},
[TOKEN_ASTERISK]        = {NULL,        &binary,    PREC_FACTOR},
[TOKEN_SLASH]           = {NULL,        &binary,    PREC_FACTOR},
[TOKEN_PERCENT]         = {NULL,        &binary,    PREC_FACTOR},
[TOKEN_CARET]           = {NULL,        &binary,    PREC_POW},

///=============================================================================

// key                  :  prefix_fn    infix_fn    precedence
[TOKEN_EQUAL]           = {NULL,        &binary,    PREC_EQUALITY},
[TOKEN_EQUAL_EQUAL]     = {NULL,        &binary,    PREC_EQUALITY},
[TOKEN_TILDE_EQUAL]     = {NULL,        &binary,    PREC_COMPARISON},
[TOKEN_ANGLE_L]         = {NULL,        &binary,    PREC_COMPARISON},
[TOKEN_ANGLE_L_EQUAL]   = {NULL,        &binary,    PREC_COMPARISON},
[TOKEN_ANGLE_R]         = {NULL,        &binary,    PREC_COMPARISON},
[TOKEN_ANGLE_R_EQUAL]   = {NULL,        &binary,    PREC_COMPARISON},
[TOKEN_IDENTIFIER]      = {NULL,        NULL,       PREC_NONE},
[TOKEN_STRING_LIT]      = {&literal,    NULL,       PREC_NONE},
[TOKEN_NUMBER_LIT]      = {&literal,    NULL,       PREC_NONE},
[TOKEN_ERROR]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_EOF]             = {NULL,        NULL,       PREC_NONE},

};

#if defined __GNUC__
    #pragma GCC diagnostic pop
#endif

void
lulu_Parse_expression(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser)
{
    parse_precedence(compiler, lexer, parser, PREC_ASSIGNMENT + 1);
}

static void
parse_infix(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser, lulu_Precedence precedence)
{
    while (precedence <= get_rule(parser->current.type)->precedence) {
        lulu_Parse_advance_token(lexer, parser);
        lulu_ParseFn infix_rule = get_rule(parser->consumed.type)->infix_fn;
        infix_rule(compiler, lexer, parser);
    }
}

static void
parse_precedence(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser, lulu_Precedence precedence)
{
    lulu_Parse_advance_token(lexer, parser);
    lulu_ParseFn prefix_rule = get_rule(parser->consumed.type)->prefix_fn;
    if (!prefix_rule) {
        lulu_Parse_error_consumed(lexer, parser, "Expected prefix expression");
        return;
    }
    prefix_rule(compiler, lexer, parser);
    parse_infix(compiler, lexer, parser, precedence);
}

static lulu_Parse_Rule *
get_rule(lulu_Token_Type type)
{
    return &LULU_PARSE_RULES[type];
}
