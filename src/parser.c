/// local
#include "parser.h"
#include "vm.h"
#include "object.h"

/// standard
#include <stdio.h>      // printf

/**
 * Because C++ is an annoying little s@%!, we have to explicitly define operations
 * even for basic enum types, even if they're contiguous.
 */
#if defined(__cplusplus)

static lulu_Precedence
operator+(lulu_Precedence prec, int addend)
{
    int result = cast(int)prec + addend;
    return cast(lulu_Precedence)result;
}

// unused 'int' argument indicates this is postfix
static lulu_Precedence
operator++(lulu_Precedence &prec, int)
{
    prec = operator+(prec, 1);
    return prec;
}

#endif // __cplusplus

static void
expression(lulu_Parser *self, lulu_Lexer *lexer, lulu_Compiler *compiler);

static void
statement(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler);

static void
parse_precedence(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler, lulu_Precedence precedence);

static lulu_Parser_Rule *
get_rule(lulu_Token_Type type);

static int
parse_expression_list(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler)
{
    int count = 0;
    do {
        expression(parser, lexer, compiler);
        count++;
    } while (lulu_Parser_match_token(parser, lexer, TOKEN_COMMA));
    return count;
}

static byte3
identifier_constant(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler, lulu_Token *token)
{
    lulu_Value tmp;
    lulu_Value_set_string(&tmp, lulu_String_new(compiler->vm, token->start, token->len));
    return lulu_Compiler_make_constant(compiler, lexer, parser, &tmp);
}

LULU_ATTR_UNUSED
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

LULU_ATTR_UNUSED
static void
print_parser(const lulu_Parser *parser)
{
    print_token(&parser->consumed, "consumed");
    printf("\t");
    print_token(&parser->current, "current");
    printf("\n");
}

void
lulu_Parser_advance_token(lulu_Parser *self, lulu_Lexer *lexer)
{
    lulu_Token token = lulu_Lexer_scan_token(lexer);

    // Should be normally impossible, but just in case
    if (token.type == TOKEN_ERROR) {
        lulu_Parser_error_current(self, lexer, "Unhandled error token");
    }

    self->consumed = self->current;
    self->current  = token;
    // print_parser(parser); //! DEBUG
}

static bool
check_token(lulu_Parser *parser, lulu_Token_Type type)
{
    return parser->current.type == type;
}

void
lulu_Parser_consume_token(lulu_Parser *self, lulu_Lexer *lexer, lulu_Token_Type type, cstring msg)
{
    if (check_token(self, type)) {
        lulu_Parser_advance_token(self, lexer);
        return;
    }
    char buf[256];
    int  len = snprintf(buf, size_of(buf), "Expected '%s' %s",
        LULU_TOKEN_STRINGS[type].data, msg);
    buf[len] = '\0';
    lulu_Parser_error_current(self, lexer, buf);
}

bool
lulu_Parser_match_token(lulu_Parser *self, lulu_Lexer *lexer, lulu_Token_Type type)
{
    if (check_token(self, type)) {
        lulu_Parser_advance_token(self, lexer);
        return true;
    }
    return false;
}

LULU_ATTR_NORETURN
static void
wrap_error(lulu_VM *vm, cstring filename, const lulu_Token *token, cstring msg)
{
    const char *where = token->start;
    isize       len   = token->len;
    if (token->type == TOKEN_EOF) {
        const Char_Slice str = LULU_TOKEN_STRINGS[TOKEN_EOF];
        where = str.data;
        len   = str.len;
    }
    lulu_VM_comptime_error(vm, filename, token->line, msg, where, len);
}

void
lulu_Parser_error_current(lulu_Parser *self, lulu_Lexer *lexer, cstring msg)
{
    wrap_error(lexer->vm, lexer->filename, &self->current, msg);
}

void
lulu_Parser_error_consumed(lulu_Parser *self, lulu_Lexer *lexer, cstring msg)
{
    wrap_error(lexer->vm, lexer->filename, &self->consumed, msg);
}

static lulu_OpCode
get_binary_op(lulu_Token_Type type)
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
concat(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler)
{
    int argc = 1;
    for (;;) {
        if (argc >= cast(int)LULU_MAX_BYTE) {
            lulu_Parser_error_consumed(parser, lexer, "Too many consecutive concatenations");
        }
        parse_precedence(parser, lexer, compiler, PREC_CONCAT + 1);
        argc++;
        if (!lulu_Parser_match_token(parser, lexer, TOKEN_ELLIPSIS_2)) {
            break;
        }
    }
    lulu_Compiler_emit_byte1(compiler, parser, OP_CONCAT, argc);
}

static void
binary(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler)
{
    lulu_Token_Type type = parser->consumed.type;
    lulu_Precedence prec = get_rule(type)->precedence;
    // For exponentiation, enforce right associativity.
    if (prec != PREC_POW) {
        prec++;
    }
    parse_precedence(parser, lexer, compiler, prec);
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
literal(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler)
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

static void
named_variable(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler, lulu_Token *ident)
{
    byte3 arg = identifier_constant(parser, lexer, compiler, ident);
    lulu_Compiler_emit_byte3(compiler, parser, OP_GETGLOBAL, arg);
}

static void
identifier(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler)
{
    named_variable(parser, lexer, compiler, &parser->consumed);
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
grouping(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler)
{
    expression(parser, lexer, compiler);
    lulu_Parser_consume_token(parser, lexer, TOKEN_PAREN_R, "after expression");
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
unary(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler)
{
    // Saved in stack frame memory as recursion will update parser->consumed.
    lulu_Token_Type type = parser->consumed.type;

    // Compile the operand.
    parse_precedence(parser, lexer, compiler, PREC_UNARY);
    lulu_Compiler_emit_opcode(compiler, parser, get_unary_opcode(type));
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

static lulu_Parser_Rule
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
expression(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler)
{
    parse_precedence(parser, lexer, compiler, PREC_ASSIGNMENT + 1);
}

static void
print_statement(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler)
{
    int argc = 0;
    lulu_Parser_consume_token(parser, lexer, TOKEN_PAREN_L, "after 'print'");
    // Potentially have at least 1 argument?
    if (!lulu_Parser_match_token(parser, lexer, TOKEN_PAREN_R)) {
        argc = parse_expression_list(parser, lexer, compiler);
        lulu_Parser_consume_token(parser, lexer, TOKEN_PAREN_R, "to close call");
    }
    lulu_Compiler_emit_byte1(compiler, parser, OP_PRINT, argc);
}

static void
statement(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler)
{
    if (lulu_Parser_match_token(parser, lexer, TOKEN_PRINT)) {
        print_statement(parser, lexer, compiler);
    } else {
        lulu_Parser_error_current(parser, lexer, "Expected an expression");
    }
    lulu_Parser_match_token(parser, lexer, TOKEN_SEMICOLON);
}

static int
count_assignment_targets(lulu_Assign *last)
{
    lulu_Assign *iter = last;
    int          count = 0;
    while (iter) {
        iter = iter->prev;
        count++;
    }
    return count;
}

/**
 * @note 2024-10-05
 *      Analogous to 'compiler.c:varDeclaration()' in the book.
 *      Assumes we just consumed an identifier.
 */
static void
assignment(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler)
{
    lulu_Assign lvalue;
    lvalue.prev  = parser->assignments;
    lvalue.op    = OP_SETGLOBAL;
    lvalue.index = identifier_constant(parser, lexer, compiler, &parser->consumed);

    /**
     * If we have a multiple assignment, resolve all the assignment targets and
     * store them in a linked list which is 'allocated' using recursive stack
     * frames.
     */
    if (lulu_Parser_match_token(parser, lexer, TOKEN_COMMA)) {
        lulu_Parser_consume_token(parser, lexer, TOKEN_IDENTIFIER, "after ','");
        parser->assignments = &lvalue;
        assignment(parser, lexer, compiler);
    }

    /**
     * For multiple assignment this should only pass true for the deepest
     * recursive call.
     *
     * That is, 'lvalue' is currently the rightmost (and last) assignment target.
     */
    if (lulu_Parser_match_token(parser, lexer, TOKEN_EQUAL)) {
        int assigns = count_assignment_targets(&lvalue);
        int exprs   = parse_expression_list(parser, lexer, compiler);

        /**
         * @details assigns > exprs
         *      a, b, c = 1, 2
         *
         * @details assigns < exprs
         *      a, b    = 1, 2, 3
         */
        if (assigns > exprs) {
            int nil_count = assigns - exprs;
            lulu_Compiler_emit_byte1(compiler, parser, OP_NIL, nil_count);
        } else if (assigns < exprs) {
            int pop_count = exprs - assigns;
            lulu_Compiler_emit_byte1(compiler, parser, OP_POP, pop_count);
        }

        lulu_Assign *iter = &lvalue;
        while (iter) {
            // printf("lvalue{prev=%p, index=%u}\n", cast(void *)iter->prev, iter->index); //! DEBUG
            lulu_Compiler_emit_byte3(compiler, parser, iter->op, iter->index);
            iter = iter->prev;
        }

        lulu_Parser_match_token(parser, lexer, TOKEN_SEMICOLON);
    }
}

void
lulu_Parser_declaration(lulu_Parser *self, lulu_Lexer *lexer, lulu_Compiler *compiler)
{
    self->assignments = NULL;
    if (lulu_Parser_match_token(self, lexer, TOKEN_IDENTIFIER)) {
        assignment(self, lexer, compiler);
    } else {
        statement(self, lexer, compiler);
    }
}

static void
parse_infix(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler, lulu_Precedence precedence)
{
    while (precedence <= get_rule(parser->current.type)->precedence) {
        lulu_Parser_advance_token(parser, lexer);
        lulu_ParseFn infix_rule = get_rule(parser->consumed.type)->infix_fn;
        infix_rule(parser, lexer, compiler);
    }
}

static void
parse_precedence(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler, lulu_Precedence precedence)
{
    lulu_Parser_advance_token(parser, lexer);
    lulu_ParseFn prefix_rule = get_rule(parser->consumed.type)->prefix_fn;
    if (!prefix_rule) {
        lulu_Parser_error_consumed(parser, lexer, "Expected prefix expression");
        return;
    }
    prefix_rule(parser, lexer, compiler);
    parse_infix(parser, lexer, compiler, precedence);
}

static lulu_Parser_Rule *
get_rule(lulu_Token_Type type)
{
    return &LULU_PARSE_RULES[type];
}
