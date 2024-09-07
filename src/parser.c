#include "parser.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>

static void parse_precedence(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser, lulu_Precedence precedence);
static lulu_Parse_Rule *get_rule(lulu_Token_Type type);

// static void print_token(const lulu_Token *token, cstring name)
// {
//     const String lexeme = token->lexeme;
//     printf(
//         "%s{type=%i, lexeme={\"%.*s\"},line=%i}",
//         name,
//         cast(int)token->type,
//         cast(int)lexeme.len,
//         lexeme.data,
//         token->line);
// }

// static void print_parser(const lulu_Parser *self)
// {
//     print_token(&self->consumed, "consumed");
//     printf("\t");
//     print_token(&self->current, "current");
//     printf("\n");
// }

void lulu_Parse_advance_token(lulu_Lexer *lexer, lulu_Parser *parser)
{
    lulu_Token token = lulu_Lexer_scan_token(lexer);
    
    // Should be normally impossible, but just in case
    if (token.type == TOKEN_ERROR) {
        lulu_Parse_error_current(lexer->vm, parser, "Unhandled error token");
    }
    parser->consumed = parser->current;
    parser->current  = token;
    // print_parser(parser); //!DEBUG
}

void lulu_Parse_consume_token(lulu_Lexer *lexer, lulu_Parser *parser, lulu_Token_Type type, cstring msg)
{
    if (parser->current.type == type) {
        lulu_Parse_advance_token(lexer, parser);
        return;
    }
    lulu_Parse_error_current(lexer->vm, parser, msg);
}

noreturn
static void wrap_error(lulu_VM *vm, const lulu_Token *token, cstring msg)
{
    String where = token->lexeme;
    if (token->type == TOKEN_EOF) {
        where = String_literal("<eof>");
    }
    lulu_VM_comptime_error(vm, token->line, msg, where);
}

void lulu_Parse_error_current(lulu_VM *vm, lulu_Parser *parser, cstring msg)
{
    wrap_error(vm, &parser->current, msg);
}

void lulu_Parse_error_consumed(lulu_VM *vm, lulu_Parser *parser, cstring msg)
{
    wrap_error(vm, &parser->consumed, msg);
}

static void binary(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser)
{
    lulu_Token_Type type = parser->consumed.type;
    lulu_Precedence prec = get_rule(type)->precedence;
    
    // For exponentiation, enforce right associativity.
    if (prec == PREC_POW) {
        parse_precedence(compiler, lexer, parser, prec);
    } else {
        parse_precedence(compiler, lexer, parser, prec + 1);
    }

    switch (type) {
    case TOKEN_PLUS:        lulu_Compiler_emit_byte(compiler, parser, OP_ADD); break;
    case TOKEN_DASH:        lulu_Compiler_emit_byte(compiler, parser, OP_SUB); break;
    case TOKEN_ASTERISK:    lulu_Compiler_emit_byte(compiler, parser, OP_MUL); break;
    case TOKEN_SLASH:       lulu_Compiler_emit_byte(compiler, parser, OP_DIV); break;
    case TOKEN_PERCENT:     lulu_Compiler_emit_byte(compiler, parser, OP_MOD); break;
    case TOKEN_CARET:       lulu_Compiler_emit_byte(compiler, parser, OP_POW); break;
    default:
        return; // Unreachable!
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
static void grouping(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser)
{
    lulu_Parse_expression(compiler, lexer, parser);
    lulu_Parse_consume_token(lexer, parser, TOKEN_PAREN_R, "Expected ')' after expression");
}

static void number(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser)
{
    char       *end;
    String      lexeme = parser->consumed.lexeme;
    lulu_Number value  = strtod(lexeme.data, &end);
    lulu_Value  tmp;
    unused(lexer);
    // We failed to convert the entire lexeme?
    if (end != (lexeme.data + lexeme.len)) {
        lulu_Parse_error_consumed(compiler->vm, parser, "Malformed number");
        return;
    }
    lulu_Value_set_number(&tmp, value);
    lulu_Compiler_emit_constant(compiler, parser, &tmp);
}

/**
 * @note 2024-09-07
 *      Assumes we just consumed some unary operator, like '-' or '#'.
 */
static void unary(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser)
{
    // Saved in stack frame memory as recursion will update `parser->consumed`.
    lulu_Token_Type type = parser->consumed.type;
    
    // Compile the operand.
    parse_precedence(compiler, lexer, parser, PREC_UNARY);

    switch (type) {
    case TOKEN_DASH: lulu_Compiler_emit_byte(compiler, parser, OP_UNM); break;
    default:
        return; // Unreachable!
    }
}


lulu_Parse_Rule LULU_PARSE_RULES[] = {
///--- RESERVED WORDS ----------------------------------------------------- {{{1

// key                  :  prefix_fn    infix_fn    precedence
[TOKEN_AND]             = {NULL,        NULL,       PREC_NONE},
[TOKEN_BREAK]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_DO]              = {NULL,        NULL,       PREC_NONE},
[TOKEN_ELSE]            = {NULL,        NULL,       PREC_NONE},
[TOKEN_ELSEIF]          = {NULL,        NULL,       PREC_NONE},
[TOKEN_END]             = {NULL,        NULL,       PREC_NONE},
[TOKEN_FALSE]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_FOR]             = {NULL,        NULL,       PREC_NONE},
[TOKEN_FUNCTION]        = {NULL,        NULL,       PREC_NONE},
[TOKEN_IF]              = {NULL,        NULL,       PREC_NONE},
[TOKEN_IN]              = {NULL,        NULL,       PREC_NONE},
[TOKEN_LOCAL]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_NIL]             = {NULL,        NULL,       PREC_NONE},
[TOKEN_NOT]             = {NULL,        NULL,       PREC_NONE},
[TOKEN_OR]              = {NULL,        NULL,       PREC_NONE},
[TOKEN_PRINT]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_REPEAT]          = {NULL,        NULL,       PREC_NONE},
[TOKEN_RETURN]          = {NULL,        NULL,       PREC_NONE},
[TOKEN_THEN]            = {NULL,        NULL,       PREC_NONE},
[TOKEN_TRUE]            = {NULL,        NULL,       PREC_NONE},
[TOKEN_UNTIL]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_WHILE]           = {NULL,        NULL,       PREC_NONE},

///--- 1}}} --------------------------------------------------------------------

///--- SINGLE-CHARACTER TOKENS -------------------------------------------- {{{1

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
[TOKEN_ELLIPSIS_2]      = {NULL,        NULL,       PREC_NONE},
[TOKEN_PERIOD]          = {NULL,        NULL,       PREC_NONE},
[TOKEN_HASH]            = {NULL,        NULL,       PREC_NONE},
[TOKEN_PLUS]            = {NULL,        &binary,    PREC_TERM},
[TOKEN_DASH]            = {&unary,      &binary,    PREC_TERM},
[TOKEN_ASTERISK]        = {NULL,        &binary,    PREC_FACTOR},
[TOKEN_SLASH]           = {NULL,        &binary,    PREC_FACTOR},
[TOKEN_PERCENT]         = {NULL,        &binary,    PREC_FACTOR},
[TOKEN_CARET]           = {NULL,        &binary,    PREC_POW},

///--- 1}}} --------------------------------------------------------------------

// key                  :  prefix_fn    infix_fn    precedence
[TOKEN_EQUAL]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_EQUAL_EQUAL]     = {NULL,        NULL,       PREC_NONE},
[TOKEN_TILDE_EQUAL]     = {NULL,        NULL,       PREC_NONE},
[TOKEN_ANGLE_L]         = {NULL,        NULL,       PREC_NONE},
[TOKEN_ANGLE_EQUAL_L]   = {NULL,        NULL,       PREC_NONE},
[TOKEN_ANGLE_R]         = {NULL,        NULL,       PREC_NONE},
[TOKEN_ANGLE_EQUAL_R]   = {NULL,        NULL,       PREC_NONE},
[TOKEN_IDENTIFIER]      = {NULL,        NULL,       PREC_NONE},
[TOKEN_STRING_LIT]      = {NULL,        NULL,       PREC_NONE},
[TOKEN_NUMBER_LIT]      = {&number,     NULL,       PREC_NONE},
[TOKEN_ERROR]           = {NULL,        NULL,       PREC_NONE},
[TOKEN_EOF]             = {NULL,        NULL,       PREC_NONE},

};

void lulu_Parse_expression(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser)
{
    parse_precedence(compiler, lexer, parser, PREC_ASSIGNMENT + 1);
}

static void parse_prefix(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser)
{
    lulu_ParseFn prefix_rule = get_rule(parser->consumed.type)->prefix_fn;
    if (!prefix_rule) {
        lulu_Parse_error_consumed(compiler->vm, parser, "Expected prefix expression");
        return;
    }
    prefix_rule(compiler, lexer, parser);
}

static void parse_infix(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser, lulu_Precedence precedence)
{
    while (precedence <= get_rule(parser->current.type)->precedence) {
        lulu_ParseFn infix_rule;
        lulu_Parse_advance_token(lexer, parser);
        infix_rule = get_rule(parser->consumed.type)->infix_fn;
        infix_rule(compiler, lexer, parser);
    }
}

static void parse_precedence(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser, lulu_Precedence precedence)
{
    lulu_Parse_advance_token(lexer, parser);
    parse_prefix(compiler, lexer, parser);
    parse_infix(compiler, lexer, parser, precedence);
}

static lulu_Parse_Rule *get_rule(lulu_Token_Type type)
{
    return &LULU_PARSE_RULES[type];
}
