#include "compiler.h"
#include "vm.h"

#ifdef LULU_DEBUG_PRINT
#include "debug.h"
#endif

#include <stdio.h>
#include <stdlib.h>

static void expression(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser);
static void parse_precedence(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser, lulu_Precedence precedence);
static lulu_Parse_Rule *get_parse_rule(lulu_Token_Type type);

void lulu_Compiler_init(lulu_VM *vm, lulu_Compiler *self, lulu_Lexer *lexer)
{
    self->vm    = vm;
    self->lexer = lexer;
}

/**
 * @note 2024-09-07
 *      This will get more complicated later on, hence we abstract it out now.
 */
static lulu_Chunk *current_chunk(lulu_Compiler *self)
{
    return self->chunk;
}

static void error_at_current(lulu_Compiler *self, lulu_Parser *parser, cstring msg)
{
    lulu_VM_comptime_error(self->vm, &parser->current, msg);
}

/**
 * @note 2024-09-07
 *      Analogous to the book's `compiler.c:error()`.
 */
static void error_at_consumed(lulu_Compiler *self, lulu_Parser *parser, cstring msg)
{
    lulu_VM_comptime_error(self->vm, &parser->consumed, msg);
}

static void print_token(const lulu_Token *token, cstring name)
{
    const String lexeme = token->lexeme;
    printf(
        "%s{type=%i, lexeme={\"%.*s\"},line=%i}",
        name,
        cast(int)token->type,
        cast(int)lexeme.len,
        lexeme.data,
        token->line);
}

static void print_parser(const lulu_Parser *self)
{
    print_token(&self->consumed, "consumed");
    printf("\t");
    print_token(&self->current, "current");
    printf("\n");
}


/**
 * @note 2024-09-06
 *      Analogous to the Book's `compiler.c:advance()`.
 */
static void next_token(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser)
{
    lulu_Token token = lulu_Lexer_scan_token(lexer);
    
    // Should be normally impossible, but just in case
    if (token.type == TOKEN_ERROR) {
        error_at_current(self, parser, "Unhandled error token");
    }
    parser->consumed = parser->current;
    parser->current  = token;
    // print_parser(parser); //!DEBUG
}

/**
 * @note 2024-09-07
 *      Analogous to the book's `compiler.c:consume()`.
 */
static void consume_token(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser, lulu_Token_Type type, cstring msg)
{
    if (parser->current.type == type) {
        next_token(self, lexer, parser);
        return;
    }
    error_at_current(self, parser, msg);
}

static void emit_byte(lulu_Compiler *self, lulu_Parser *parser, byte inst)
{
    lulu_Chunk_write(self->vm, current_chunk(self), inst, parser->consumed.line);
}

static void emit_2_bytes(lulu_Compiler *self, lulu_Parser *parser, byte inst1, byte inst2)
{
    emit_byte(self, parser, inst1);
    emit_byte(self, parser, inst2);
}

static void emit_byte3(lulu_Compiler *self, lulu_Parser *parser, usize byte3)
{
    lulu_Chunk_write_byte3(self->vm, current_chunk(self), byte3, parser->consumed.line);
}

static void emit_return(lulu_Compiler *self, lulu_Parser *parser)
{
    emit_byte(self, parser, OP_RETURN);
}

#define LULU_MAX_CONSTANTS  (1 << 24)

static isize make_constant(lulu_Compiler *self, lulu_Parser *parser, const lulu_Value *value)
{
    isize index = lulu_Chunk_add_constant(self->vm, current_chunk(self), value);
    if (index > LULU_MAX_CONSTANTS) {
        error_at_consumed(self, parser, "Too many constants in one chunk.");
        return 0;
    }
    return index;
}

static void emit_constant(lulu_Compiler *self, lulu_Parser *parser, const lulu_Value *value)
{
    isize index = make_constant(self, parser, value);
    emit_byte(self, parser, OP_CONSTANT);
    emit_byte3(self, parser, index);
}

static void end_compiler(lulu_Compiler *self, lulu_Parser *parser)
{
    emit_return(self, parser);
#ifdef LULU_DEBUG_PRINT
    lulu_Debug_disasssemble_chunk(current_chunk(self), "code");
#endif
}

static void binary(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser)
{
    lulu_Token_Type type = parser->consumed.type;
    lulu_Precedence prec = get_parse_rule(type)->precedence;
    
    // For exponentiation, enforce right associativity.
    if (prec == PREC_POW) {
        parse_precedence(self, lexer, parser, prec);
    } else {
        parse_precedence(self, lexer, parser, prec + 1);
    }
    switch (type) {
    case TOKEN_PLUS:        emit_byte(self, parser, OP_ADD); break;
    case TOKEN_DASH:        emit_byte(self, parser, OP_SUB); break;
    case TOKEN_ASTERISK:    emit_byte(self, parser, OP_MUL); break;
    case TOKEN_SLASH:       emit_byte(self, parser, OP_DIV); break;
    case TOKEN_PERCENT:     emit_byte(self, parser, OP_MOD); break;
    case TOKEN_CARET:       emit_byte(self, parser, OP_POW); break;
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
static void grouping(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser)
{
    expression(self, lexer, parser);
    consume_token(self, lexer, parser, TOKEN_PAREN_R, "Expected ')' after expression.");
}

static void number(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser)
{
    char       *end;
    String      lexeme = parser->consumed.lexeme;
    lulu_Number value = strtod(lexeme.data, &end);
    lulu_Value  tmp;
    unused(lexer);
    // We failed to convert the entire lexeme?
    if (end != (lexeme.data + lexeme.len)) {
        error_at_consumed(self, parser, "Malformed number");
        return;
    }
    lulu_Value_set_number(&tmp, value);
    emit_constant(self, parser, &tmp);
}

/**
 * @note 2024-09-07
 *      Assumes we just consumed some unary operator, like '-' or '#'.
 */
static void unary(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser)
{
    // Saved in stack frame memory as recursion will update `parser->consumed`.
    lulu_Token_Type type = parser->consumed.type;
    
    // Compile the operand.
    parse_precedence(self, lexer, parser, PREC_UNARY);

    switch (type) {
    case TOKEN_DASH: emit_byte(self, parser, OP_UNM); break;
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

static void parse_precedence(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser, lulu_Precedence precedence)
{
    next_token(self, lexer, parser);
    lulu_ParseFn prefix_rule = get_parse_rule(parser->consumed.type)->prefix_fn;
    if (!prefix_rule) {
        error_at_consumed(self, parser, "Expected prefix expression");
        return;
    }
    prefix_rule(self, lexer, parser);
    
    while (precedence <= get_parse_rule(parser->current.type)->precedence) {
        lulu_ParseFn infix_rule;
        next_token(self, lexer, parser);
        infix_rule = get_parse_rule(parser->consumed.type)->infix_fn;
        infix_rule(self, lexer, parser);
    }
}

static lulu_Parse_Rule *get_parse_rule(lulu_Token_Type type)
{
    return &LULU_PARSE_RULES[type];
}

static void expression(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser)
{
    parse_precedence(self, lexer, parser, PREC_ASSIGNMENT + 1);
}

void lulu_Compiler_compile(lulu_Compiler *self, cstring input, lulu_Chunk *chunk)
{
    lulu_Lexer *lexer  = self->lexer;
    lulu_Parser parser = {0};
    self->chunk = chunk;
    lulu_Lexer_init(self->vm, self->lexer, input);
    next_token(self, lexer, &parser);
    expression(self, lexer, &parser);
    consume_token(self, lexer, &parser, TOKEN_EOF, "Expected end of expression");
    end_compiler(self, &parser);
}
