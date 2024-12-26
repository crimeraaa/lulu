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
expression(lulu_Parser *self);

static void
statement(lulu_Parser *parser);

static void
parse_precedence(lulu_Parser *parser, lulu_Precedence precedence);

static void
parse_infix(lulu_Parser *parser, lulu_Precedence precedence);

static lulu_Parser_Rule *
get_rule(lulu_Token_Type type);

/**
 * @note 2024-12-13
 *      Assumes we just consumed a '=' token.
 */
static int
parse_expression_list(lulu_Parser *parser)
{
    int count = 0;
    do {
        expression(parser);
        count++;
    } while (lulu_Parser_match_token(parser, TOKEN_COMMA));
    return count;
}

static byte3
identifier_constant(lulu_Parser *parser, lulu_Token *token)
{
    lulu_Compiler *compiler = parser->compiler;
    lulu_Value     tmp;
    lulu_String   *string = lulu_String_new(compiler->vm, token->start, token->len);
    lulu_Value_set_string(&tmp, string);
    return lulu_Compiler_make_constant(compiler, &tmp);
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
lulu_Parser_init(lulu_Parser *self, lulu_Compiler *compiler, lulu_Lexer *lexer)
{
    lulu_Token_init_empty(&self->consumed);
    lulu_Token_init_empty(&self->current);
    self->assignments = NULL;
    self->compiler    = compiler;
    self->lexer       = lexer;
}

void
lulu_Parser_advance_token(lulu_Parser *self)
{
    lulu_Lexer    *lexer    = self->lexer;
    lulu_Token     token    = lulu_Lexer_scan_token(lexer);

    // Should be normally impossible, but just in case
    if (token.type == TOKEN_ERROR) {
        lulu_Parser_error_current(self, "Unhandled error token");
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
lulu_Parser_consume_token(lulu_Parser *self, lulu_Token_Type type, cstring msg)
{
    if (lulu_Parser_match_token(self, type)) {
        return;
    }
    char buf[256];
    int  len = snprintf(buf, size_of(buf), "Expected '%s' %s",
        LULU_TOKEN_STRINGS[type].data, msg);
    buf[len] = '\0';
    lulu_Parser_error_current(self, buf);
}

bool
lulu_Parser_match_token(lulu_Parser *self, lulu_Token_Type type)
{
    if (check_token(self, type)) {
        lulu_Parser_advance_token(self);
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
        const lulu_Token_String str = LULU_TOKEN_STRINGS[TOKEN_EOF];
        where = str.data;
        len   = str.len;
    }
    lulu_VM_comptime_error(vm, filename, token->line, msg, where, len);
}

void
lulu_Parser_error_current(lulu_Parser *self, cstring msg)
{
    lulu_Lexer *lexer = self->lexer;
    wrap_error(lexer->vm, lexer->filename, &self->current, msg);
}

void
lulu_Parser_error_consumed(lulu_Parser *self, cstring msg)
{
    lulu_Lexer *lexer = self->lexer;
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
concat(lulu_Parser *parser)
{
    lulu_Compiler *compiler = parser->compiler;
    int            argc = 1;
    for (;;) {
        if (argc >= cast(int)LULU_MAX_BYTE) {
            lulu_Parser_error_consumed(parser, "Too many consecutive concatenations");
        }
        parse_precedence(parser, PREC_CONCAT + 1);
        argc++;
        if (!lulu_Parser_match_token(parser, TOKEN_ELLIPSIS_2)) {
            break;
        }
    }
    lulu_Compiler_emit_byte1(compiler, OP_CONCAT, argc);
}

static void
binary(lulu_Parser *parser)
{
    lulu_Compiler  *compiler = parser->compiler;
    lulu_Token_Type type     = parser->consumed.type;
    lulu_Precedence prec     = get_rule(type)->precedence;
    // For exponentiation, enforce right associativity.
    if (prec != PREC_POW) {
        prec++;
    }
    parse_precedence(parser, prec);
    lulu_Compiler_emit_opcode(compiler, get_binary_op(type));

    // NOT, GT and GEQ are implemented as complements of EQ, LEQ and LT.
    switch (type) {
    case TOKEN_TILDE_EQUAL:
    case TOKEN_ANGLE_R:
    case TOKEN_ANGLE_R_EQUAL:
        lulu_Compiler_emit_opcode(compiler, OP_NOT);
        break;
    default:
        break;
    }

}

static void
literal(lulu_Parser *parser)
{
    lulu_Compiler *compiler = parser->compiler;
    lulu_Lexer    *lexer    = parser->lexer;
    switch (parser->consumed.type) {
    case TOKEN_FALSE:
        lulu_Compiler_emit_opcode(compiler, OP_FALSE);
        break;
    case TOKEN_NIL:
        lulu_Compiler_emit_byte1(compiler, OP_NIL, 1);
        break;
    case TOKEN_TRUE:
        lulu_Compiler_emit_opcode(compiler, OP_TRUE);
        break;
    case TOKEN_STRING_LIT: {
        lulu_Value tmp;
        lulu_Value_set_string(&tmp, lexer->string);
        lulu_Compiler_emit_constant(compiler, &tmp);
        break;
    }
    case TOKEN_NUMBER_LIT: {
        lulu_Compiler_emit_number(compiler, lexer->number);
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
named_variable(lulu_Parser *parser, lulu_Token *ident)
{
    lulu_Compiler *compiler = parser->compiler;
    const isize    local    = lulu_Compiler_resolve_local(compiler, &parser->consumed);

    if (local == UNRESOLVED_LOCAL) {
        byte3 global = identifier_constant(parser, ident);
        lulu_Compiler_emit_byte3(compiler, OP_GETGLOBAL, global);
    } else {
        lulu_Compiler_emit_byte1(compiler, OP_GETLOCAL, local);
    }

    for (;;) {
        if (lulu_Parser_match_token(parser, TOKEN_PERIOD)) {
            lulu_Parser_consume_token(parser, TOKEN_IDENTIFIER, "after '.'");
            lulu_Compiler_emit_string(compiler, &parser->consumed);
            lulu_Compiler_emit_opcode(compiler, OP_GETTABLE);
        } else if (lulu_Parser_match_token(parser, TOKEN_BRACKET_L)) {
            expression(parser);
            lulu_Compiler_emit_opcode(compiler, OP_GETTABLE);
            lulu_Parser_consume_token(parser, TOKEN_BRACKET_R, "to close '['");
        } else {
            break;
        }
    }
}

static void
identifier(lulu_Parser *parser)
{
    named_variable(parser, &parser->consumed);
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
grouping(lulu_Parser *parser)
{
    expression(parser);
    lulu_Parser_consume_token(parser, TOKEN_PAREN_R, "after expression");
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
unary(lulu_Parser *parser)
{
    // Saved in stack frame memory as recursion will update parser->consumed.
    lulu_Token_Type type     = parser->consumed.type;
    lulu_Compiler  *compiler = parser->compiler;

    // Compile the operand.
    parse_precedence(parser, PREC_UNARY);
    lulu_Compiler_emit_opcode(compiler, get_unary_opcode(type));
}

/**
 * @note 2024-12-10
 *      Assumes we just consumed a '{'.
 */
static void
table(lulu_Parser *parser)
{
    isize          n_fields = 0;
    lulu_Compiler *compiler = parser->compiler;

    int i_table = compiler->stack_usage;
    int n_array = 0;

    isize i_code = lulu_Compiler_new_table(compiler);
    // Have 1 or more fields to deal with?
    while (!check_token(parser, TOKEN_CURLY_R)) {
        int i_key = compiler->stack_usage;
        if (lulu_Parser_match_token(parser, TOKEN_BRACKET_L)) {
            expression(parser);

            lulu_Parser_consume_token(parser, TOKEN_BRACKET_R, "to close '['");
            lulu_Parser_consume_token(parser, TOKEN_EQUAL, "in field assignment");
            expression(parser);
        } else if (lulu_Parser_match_token(parser, TOKEN_IDENTIFIER)) {
            // The data would otherwise be lost if we do match a '='.
            lulu_Token ident = parser->consumed;

            // Assigning a named field?
            if (lulu_Parser_match_token(parser, TOKEN_EQUAL)) {
                lulu_Compiler_emit_string(compiler, &ident);
                expression(parser);
            } else {
                n_array++;
                lulu_Compiler_emit_number(compiler, cast(lulu_Number)n_array);
                // We already consumed the prefix portion of the expression
                parse_infix(parser, PREC_NONE);
            }
        } else {
            n_array++;
            lulu_Compiler_emit_number(compiler, cast(lulu_Number)n_array);
            // Unlike the above branch we haven't consumed the prefix portion.
            expression(parser);
        }
        lulu_Compiler_set_table(compiler, i_table, i_key, 2);
        n_fields++;
        if (!lulu_Parser_match_token(parser, TOKEN_COMMA)) {
            break;
        }
    }

    lulu_Compiler_adjust_table(compiler, i_code, n_fields);
    lulu_Parser_consume_token(parser, TOKEN_CURLY_R, "to close table constructor");
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
[TOKEN_CURLY_L]         = {&table,      NULL,       PREC_NONE},
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
expression(lulu_Parser *parser)
{
    parse_precedence(parser, PREC_ASSIGNMENT + 1);
}

static void
block(lulu_Parser *parser)
{
    while (!check_token(parser, TOKEN_END) && !check_token(parser, TOKEN_EOF)) {
        lulu_Parser_declaration(parser);
    }
    lulu_Parser_consume_token(parser, TOKEN_END, "to close 'do' block");
}

static void
print_statement(lulu_Parser *parser)
{
    lulu_Compiler *compiler = parser->compiler;
    int            argc     = 0;
    lulu_Parser_consume_token(parser, TOKEN_PAREN_L, "after 'print'");
    // Potentially have at least 1 argument?
    if (!lulu_Parser_match_token(parser, TOKEN_PAREN_R)) {
        argc = parse_expression_list(parser);
        lulu_Parser_consume_token(parser, TOKEN_PAREN_R, "to close call");
    }
    lulu_Compiler_emit_byte1(compiler, OP_PRINT, argc);
}

static void
statement(lulu_Parser *parser)
{
    if (lulu_Parser_match_token(parser, TOKEN_PRINT)) {
        print_statement(parser);
    } else if (lulu_Parser_match_token(parser, TOKEN_DO)) {
        lulu_Compiler *compiler = parser->compiler;
        lulu_Compiler_begin_scope(compiler);
        block(parser);
        lulu_Compiler_end_scope(compiler);
    } else {
        lulu_Parser_error_current(parser, "Expected an expression");
    }
    lulu_Parser_match_token(parser, TOKEN_SEMICOLON);
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
 * @details assigns > exprs
 *      a, b, c = 1, 2
 *
 * @details assigns < exprs
 *      a, b    = 1, 2, 3
 */
static void
adjust_assignment_list(lulu_Parser *parser, int assigns, int exprs)
{
    lulu_Compiler *compiler = parser->compiler;
    if (assigns > exprs) {
        int nil_count = assigns - exprs;
        lulu_Compiler_emit_byte1(compiler, OP_NIL, nil_count);
    } else if (assigns < exprs) {
        int pop_count = exprs - assigns;
        lulu_Compiler_emit_byte1(compiler, OP_POP, pop_count);
    }
}

/**
 * @note 2024-12-10
 *      (Somewhat) Analogous to the book's `compiler.c:defineVariable()`.
 */
static void
emit_assignment_targets(lulu_Parser *parser, lulu_Assign *head)
{
    lulu_Compiler *compiler = parser->compiler;
    const int      assigns  = count_assignment_targets(head);
    const int      exprs    = parse_expression_list(parser);

    adjust_assignment_list(parser, assigns, exprs);

    lulu_Assign *iter = head;
    while (iter) {
        lulu_OpCode op = iter->op;
        if (op == OP_SETLOCAL) {
            lulu_Compiler_emit_byte1(compiler, op, iter->index);
        } else if (op == OP_SETGLOBAL) {
            lulu_Compiler_emit_byte3(compiler, op, iter->index);
        }
        iter = iter->prev;
    }

    lulu_Parser_match_token(parser, TOKEN_SEMICOLON);
    parser->assignments = NULL;
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
assignment(lulu_Parser *parser)
{
    lulu_Assign    lvalue;
    lulu_Compiler *compiler = parser->compiler;
    const int      local    = lulu_Compiler_resolve_local(compiler, &parser->consumed);

    if (local == UNRESOLVED_LOCAL) {
        lvalue.op    = OP_SETGLOBAL;
        lvalue.index = identifier_constant(parser, &parser->consumed);
    } else {
        lvalue.op    = OP_SETLOCAL;
        lvalue.index = local;
    }

    lvalue.prev = parser->assignments;
    parser->assignments = &lvalue; // Should end at the deepest recursive call.

    if (lulu_Parser_match_token(parser, TOKEN_PERIOD) || lulu_Parser_match_token(parser, TOKEN_BRACKET_L)) {
        lulu_Parser_error_consumed(parser, "table assignment statements not yet implemented");
    }

    /**
     * If we have a multiple assignment, resolve all the assignment targets and
     * store them in a linked list which is 'allocated' using recursive stack
     * frames.
     */
    if (lulu_Parser_match_token(parser, TOKEN_COMMA)) {
        lulu_Parser_consume_token(parser, TOKEN_IDENTIFIER, "after ','");
        assignment(parser);
    }

    if (parser->assignments) {
        lulu_Parser_consume_token(parser, TOKEN_EQUAL, "in assignment");
        emit_assignment_targets(parser, &lvalue);
    }
}

void
lulu_Parser_declaration(lulu_Parser *self)
{
    lulu_Compiler *compiler = self->compiler;

    if (lulu_Parser_match_token(self, TOKEN_LOCAL)) {
        int assigns = 0;
        int exprs   = 0;

        do {
            lulu_Parser_consume_token(self, TOKEN_IDENTIFIER, "after 'local'");
            lulu_Compiler_add_local(compiler, &self->consumed);
            assigns++;
        } while (lulu_Parser_match_token(self, TOKEN_COMMA));

        if (lulu_Parser_match_token(self, TOKEN_EQUAL)) {
            exprs = parse_expression_list(self);
        }

        adjust_assignment_list(self, assigns, exprs);
        lulu_Compiler_initialize_locals(self->compiler);
        lulu_Parser_match_token(self, TOKEN_SEMICOLON);
    } else if (lulu_Parser_match_token(self, TOKEN_IDENTIFIER)) {
        assignment(self);
    } else {
        statement(self);
    }
}

static void
parse_infix(lulu_Parser *parser, lulu_Precedence precedence)
{
    while (precedence <= get_rule(parser->current.type)->precedence) {
        lulu_Parser_advance_token(parser);
        get_rule(parser->consumed.type)->infix_fn(parser);
    }
}

static void
parse_precedence(lulu_Parser *parser, lulu_Precedence precedence)
{
    lulu_Parser_advance_token(parser);
    lulu_ParseFn prefix_rule = get_rule(parser->consumed.type)->prefix_fn;
    if (!prefix_rule) {
        lulu_Parser_error_consumed(parser, "Expected prefix expression");
        return;
    }
    prefix_rule(parser);
    parse_infix(parser, precedence);

    // Don't consume as we want to report the left hand side of the '='.
    if (check_token(parser, TOKEN_EQUAL)) {
        lulu_Parser_error_consumed(parser, "Invalid assignment target");
    }
}

static lulu_Parser_Rule *
get_rule(lulu_Token_Type type)
{
    return &LULU_PARSE_RULES[type];
}
