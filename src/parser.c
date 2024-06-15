#include "parser.h"
#include "object.h"
#include "string.h"
#include "table.h"
#include "vm.h"

// Forward declarations to allow recursive descent parsing.
static ParseRule *get_parserule(TkType key);
static void parse_precedence(Compiler *cpl, Precedence prec);

/**
 * @brief   In Lua, globals aren't declared, but rather assigned as needed.
 *          This may be inconsistent with the design that accessing undefined
 *          globals is an error, but at the same time I dislike implicit nil for
 *          undefined globals.
 *
 * @details statement ::= identlist '=' exprlist
 *                      | 'do' block 'end'
 *                      | 'print' expression
 *                      | expression
 *          identlist ::= identifier [',' identifier]*
 *          exprlist  ::= expression [',' expression]*
 *
 * @note    By themselves, statements should have zero net stack effect.
 */
static void statement(Compiler *cpl);

/**
 * @brief   By itself, always results in exactly 1 value being pushed.
 *
 * @note    We don't parse the same precedence as assignment in order to
 *          disallow C-style constructs like `print(x = 13)`, which is usually
 *          intended to be `print(x == 13)`.
 */
static void expression(Compiler *cpl);

static int parse_exprlist(Compiler *cpl)
{
    Lexer *ls    = cpl->lexer;
    int    exprs = 0;
    do {
        expression(cpl);
        exprs += 1;
    } while (luluLex_match_token(ls, TK_COMMA));
    return exprs;
}

static void adjust_exprlist(Compiler *cpl, int idents, int exprs)
{
    if (exprs == idents)
        return;

    // True: Discard extra expressions. False: Assign nils to remaining idents.
    if (exprs > idents)
        luluCpl_emit_oparg1(cpl, OP_POP, exprs - idents);
    else
        luluCpl_emit_oparg1(cpl, OP_NIL, idents - exprs);
}

// EXPRESSIONS ------------------------------------------------------------ {{{1

// INFIX ------------------------------------------------------------------ {{{2

static OpCode get_binop(TkType type)
{
    switch (type) {
    case TK_PLUS:    return OP_ADD;
    case TK_DASH:    return OP_SUB;
    case TK_STAR:    return OP_MUL;
    case TK_SLASH:   return OP_DIV;
    case TK_PERCENT: return OP_MOD;
    case TK_CARET:   return OP_POW;

    case TK_EQ:      return OP_EQ;
    case TK_LT:      return OP_LT;
    case TK_LE:      return OP_LE;
    default:
        // Should not happen
        return OP_RETURN;
    }
}

// Assumes we just consumed a binary operator as a possible infix expression,
// and that the left-hand side has been fully compiled.
static void binary(Compiler *cpl)
{
    Lexer  *ls   = cpl->lexer;
    TkType  type = ls->consumed.type;

    // For exponentiation enforce right-associativity.
    parse_precedence(cpl, get_parserule(type)->precedence + (type != TK_CARET));

    // NEQ, GT and GE must be encoded as logical NOT of their counterparts.
    switch (type) {
    case TK_NEQ:
        luluCpl_emit_opcode(cpl, OP_EQ);
        luluCpl_emit_opcode(cpl, OP_NOT);
        break;
    case TK_GT:
        luluCpl_emit_opcode(cpl, OP_LE);
        luluCpl_emit_opcode(cpl, OP_NOT);
        break;
    case TK_GE:
        luluCpl_emit_opcode(cpl, OP_LT);
        luluCpl_emit_opcode(cpl, OP_NOT);
        break;
    default:
        luluCpl_emit_opcode(cpl, get_binop(type));
        break;
    }
}

/**
 * @brief   Assumes we just consumed a `..` token and that the first argument
 *          has already been compiled.
 *
 * @note    Although right associative, we don't recursively compile concat
 *          expressions in the same grouping.
 *
 *          We do this iteratively which is (slightly) better than constantly
 *          allocating stack frames for recursive calls.
 */
static void concat(Compiler *cpl)
{
    Lexer *ls   = cpl->lexer;
    int    argc = 1;
    do {
        if (argc + 1 > MAX_BYTE)
            luluLex_error_consumed(ls, "Too many consecutive concatenations");
        parse_precedence(cpl, PREC_CONCAT + 1);
        argc += 1;
    } while (luluLex_match_token(ls, TK_CONCAT));
    luluCpl_emit_oparg1(cpl, OP_CONCAT, argc);
}

// 2}}} ------------------------------------------------------------------------

// PREFIX ----------------------------------------------------------------- {{{2

static void literal(Compiler *cpl)
{
    switch (cpl->lexer->consumed.type) {
    case TK_NIL:   luluCpl_emit_oparg1(cpl, OP_NIL, 1); break;
    case TK_TRUE:  luluCpl_emit_opcode(cpl, OP_TRUE);   break;
    case TK_FALSE: luluCpl_emit_opcode(cpl, OP_FALSE);  break;
    default:
        // Should not happen
        break;
    }
}

// Assumes we just consumed a '('.
static void grouping(Compiler *cpl)
{
    Lexer *ls = cpl->lexer;

    // Hacky to create a new scope but lets us error at too many C-facing calls.
    // See: https://www.lua.org/source/5.1/lparser.c.html#enterlevel
    luluCpl_begin_scope(cpl);
    expression(cpl);
    luluLex_expect_token(ls, TK_RPAREN, NULL);
    luluCpl_end_scope(cpl);
}

// Assumes the lexer successfully consumed and encoded a number literal.
static void number(Compiler *cpl)
{
    Lexer *ls = cpl->lexer;
    Value  v  = make_number(ls->number);
    luluCpl_emit_constant(cpl, &v);
}

static void string(Compiler *cpl)
{
    Lexer *ls = cpl->lexer;
    Value  v  = make_string(ls->string);
    luluCpl_emit_constant(cpl, &v);
}

// Assumes we consumed a `'['` or an identifier representing a table field.
static bool parse_field(Compiler *cpl, bool assigning)
{
    Lexer *ls = cpl->lexer;
    Token *tk = &ls->consumed;
    switch (tk->type) {
    case TK_LBRACKET:
        expression(cpl);
        luluLex_expect_token(ls, TK_RBRACKET, NULL);
        break;
    case TK_PERIOD:
        if (assigning) {
            return false;
        }
        luluLex_expect_token(ls, TK_IDENT, NULL); // Fall through
    case TK_IDENT:
        luluCpl_emit_identifier(cpl, tk);
        break;
    default:
        break;
    }
    if (!assigning) {
        luluCpl_emit_opcode(cpl, OP_GETTABLE);
    }
    return true;
}

static void resolve_variable(Compiler *cpl, const Token *id)
{
    Lexer *ls = cpl->lexer;
    luluCpl_emit_variable(cpl, id);
    while (luluLex_match_token_any(ls, TK_LBRACKET, TK_PERIOD)) {
        parse_field(cpl, false);
    }
}

static void parse_ctor(Compiler *cpl, int t_idx, int *count)
{
    Lexer *ls = cpl->lexer;

    if (luluLex_match_token(ls, TK_IDENT)) {
        Token id    = ls->consumed; // Copy by value as lexer might update.
        int   k_idx = cpl->stack_usage;
        if (luluLex_match_token(ls, TK_ASSIGN)) {
            luluCpl_emit_identifier(cpl, &id);
            expression(cpl);
            luluCpl_emit_oparg3(cpl, OP_SETTABLE, encode_byte3(t_idx, k_idx, 2));
        } else {
            resolve_variable(cpl, &id);
            *count += 1;
        }
    } else if (luluLex_match_token(ls, TK_LBRACKET)) {
        int k_idx = cpl->stack_usage;

        parse_field(cpl, true);
        luluLex_expect_token(ls, TK_ASSIGN, "to assign table field");
        expression(cpl);

        // Always pop the key and value assigning in a table constructor.
        luluCpl_emit_oparg3(cpl, OP_SETTABLE, encode_byte3(t_idx, k_idx, 2));
    } else {
        expression(cpl);
        *count += 1;
    }
    luluLex_match_token(ls, TK_COMMA);
}

static void table(Compiler *cpl)
{
    Lexer *ls     = cpl->lexer;
    int    t_idx  = cpl->stack_usage;
    int    total  = 0; // Array length plus hashmap length.
    int    count  = 0; // Array portion length.
    int    offset = luluCpl_emit_table(cpl);
    while (!luluLex_match_token(ls, TK_RCURLY)) {
        parse_ctor(cpl, t_idx, &count);
        total += 1;
    }

    if (count > 0) {
        if (count + 1 > MAX_BYTE3) {
            luluLex_error_consumed(ls, "Too many elements in table constructor");
        }
        luluCpl_emit_oparg2(cpl, OP_SETARRAY, encode_byte2(t_idx, count));
    }

    if (total > 0) {
        luluCpl_patch_table(cpl, offset, total);
    }
}

/**
 * @brief   Originally analogous to `namedVariable()` in the book, but with our
 *          current semantics ours is radically different.
 *
 * @note    Past the first lexeme, assigning of variables is not allowed in Lua.
 *          So this function can only ever perform get operations.
 */
static void variable(Compiler *cpl)
{
    Lexer *ls = cpl->lexer;
    Token *id = &ls->consumed;
    resolve_variable(cpl, id);
}

static OpCode get_unop(TkType type)
{
    switch (type) {
    case TK_NOT:    return OP_NOT;
    case TK_DASH:   return OP_UNM;
    case TK_POUND:  return OP_LEN;
    default:
        // Should not happen
        return OP_RETURN;
    }
}

// Assumes we just consumed an unary operator.
static void unary(Compiler *cpl)
{
    Lexer *ls   = cpl->lexer;
    TkType type = ls->consumed.type; // Save in stack-frame memory.
    parse_precedence(cpl, PREC_UNARY);
    luluCpl_emit_opcode(cpl, get_unop(type));
}

// 2}}} ------------------------------------------------------------------------

static void expression(Compiler *cpl)
{
    parse_precedence(cpl, PREC_ASSIGN + 1);
}

// 1}}} ------------------------------------------------------------------------

// STATEMENTS ------------------------------------------------------------- {{{1

// ASSIGNMENTS ------------------------------------------------------------ {{{2

static void init_assignment(Assignment *cpl, Assignment *prev, AssignType type)
{
    cpl->prev = prev;
    cpl->type = type;
    cpl->arg  = -1;
}

static void set_assignment(Assignment *cpl, AssignType type, int arg)
{
    cpl->type = type;
    cpl->arg  = arg;
}

static int count_assignments(Assignment *cpl)
{
    Assignment *node = cpl;
    int         count = 0;
    while (node != NULL) {
        node   = node->prev;
        count += 1;
    }
    return count;
}

static void emit_assignment_tail(Compiler *cpl, Assignment *list)
{
    if (list == NULL)
        return;

    switch (list->type) {
    case ASSIGN_GLOBAL:
        luluCpl_emit_oparg3(cpl, OP_SETGLOBAL, list->arg);
        break;
    case ASSIGN_LOCAL:
        luluCpl_emit_oparg1(cpl, OP_SETLOCAL, list->arg);
        break;
    case ASSIGN_TABLE:
        // For assignments we assume the key is always right after the table.
        luluCpl_emit_oparg3(cpl, OP_SETTABLE, encode_byte3(list->arg, list->arg + 1, 1));
        break;
    }
    emit_assignment_tail(cpl, list->prev);

    // After recursion, clean up stack if we emitted table fields as they cannot
    // be implicitly popped due to SETTABLE being a VAR_DELTA pop.
    if (list->type == ASSIGN_TABLE)
        luluCpl_emit_oparg1(cpl, OP_POP, 2);
}

static void emit_assignment(Compiler *cpl, Assignment *list)
{
    int idents = count_assignments(list);
    int exprs  = parse_exprlist(cpl);
    adjust_exprlist(cpl, idents, exprs);
    emit_assignment_tail(cpl, list);
}

/**
 * @brief   Emit a variable which is used as a table and the fields thereof.
 *          We do this in order to support multiple assignment semantics.
 *
 * @details In `t.k.v = 13` we want:
 *          GETGLOBAL   't'
 *          CONSTANT    'k'
 *          GETTABLE
 *          CONSTANT    'v'
 *          CONSTANT    13
 *          SETTABLE
 *
 * @note    We don't need to "hold on" to the table/fields themselves, but we do
 *          need to track where in the stack the table occurs so the SETTABLE
 *          instruction knows where to look.
 */
static void discharge_assignment(Compiler *cpl, Assignment *list, bool isfield)
{
    Lexer *ls = cpl->lexer;

    // Get the table itself if this is the first token in an lvalue, or if this
    // is part of a recursive call we need GETTABLE to push a subtable.
    switch (list->type) {
    case ASSIGN_GLOBAL:
        luluCpl_emit_oparg3(cpl, OP_GETGLOBAL, list->arg);
        break;
    case ASSIGN_LOCAL:
        luluCpl_emit_oparg1(cpl, OP_GETLOCAL,  list->arg);
        break;
    case ASSIGN_TABLE:
        luluCpl_emit_opcode(cpl, OP_GETTABLE);
        break;
    }

    // Emit the key immediately. In recursive calls we'll emit another GETTABLE,
    // otherwise we'll know the key is +1 after the table when we emit SETTABLE.
    if (isfield) {
        Token *id = &ls->consumed;
        luluLex_expect_token(ls, TK_IDENT, "after '.'");
        luluCpl_emit_identifier(cpl, id);
    } else {
        expression(cpl);
        luluLex_expect_token(ls, TK_RBRACKET, NULL);
    }

    // stack_usage - 0 is top, -1 is the key we emitted and -2 is the table.
    set_assignment(list, ASSIGN_TABLE, cpl->stack_usage - 2);
}

// Assumes we consumed an identifier as the first element of a statement.
static void identifier_statement(Compiler *cpl, Assignment *list)
{
    Lexer *ls = cpl->lexer;
    Token *id = &ls->consumed;

    if (list->type != ASSIGN_TABLE) {
        int  arg     = luluCpl_resolve_local(cpl, id);
        bool islocal = (arg != -1);
        if (!islocal) {
            arg = luluCpl_identifier_constant(cpl, id);
        }
        set_assignment(list, islocal ? ASSIGN_LOCAL : ASSIGN_GLOBAL, arg);
    }

    switch (ls->lookahead.type) {
    case TK_PERIOD: // Fall through
    case TK_LBRACKET:
        // Emit the table up to this point then mark `list` as a table.
        luluLex_next_token(ls);
        discharge_assignment(cpl, list, ls->consumed.type == TK_PERIOD);
        identifier_statement(cpl, list);
        break;
    case TK_ASSIGN:
        luluLex_next_token(ls);
        emit_assignment(cpl, list);
        break;
    case TK_LPAREN:
        luluLex_error_consumed(ls, "Function calls not yet implemented");
        break;
    case TK_COMMA: {
        // Recursive call so chain elements together.
        Assignment next;
        init_assignment(&next, list, ASSIGN_GLOBAL);
        luluLex_next_token(ls);
        luluLex_expect_token(ls, TK_IDENT, "after ','");
        identifier_statement(cpl, &next);
        break;
    }
    default:
        luluLex_error_consumed(ls, "Unexpected token in identifier statement");
    }
}

// OTHER ------------------------------------------------------------------ {{{3

// Assumes we just consumed the `print` keyword and are now ready to compile a
// stream of expressions to act as arguments.
static void print_statement(Compiler *cpl)
{
    Lexer *ls   = cpl->lexer;
    bool   open = luluLex_match_token(ls, TK_LPAREN);
    int    argc = parse_exprlist(cpl);
    if (open)
        luluLex_expect_token(ls, TK_RPAREN, NULL);
    luluCpl_emit_oparg1(cpl, OP_PRINT, argc);
}

static void block(Compiler *cpl)
{
    Lexer *ls = cpl->lexer;
    while (!luluLex_check_token_any(ls, TK_END, TK_EOF)) {
        declaration(cpl);
    }
    luluLex_expect_token(ls, TK_END, "after block");
}

// 3}}} ------------------------------------------------------------------------

// 2}}} ------------------------------------------------------------------------

static void statement(Compiler *cpl)
{
    Lexer *ls = cpl->lexer;
    switch (ls->lookahead.type) {
    case TK_IDENT: {
        Assignment list;
        init_assignment(&list, NULL, ASSIGN_GLOBAL); // Ensure no garbage.
        luluLex_next_token(ls);
        identifier_statement(cpl, &list);
        break;
    }
    case TK_DO:
        luluLex_next_token(ls);
        luluCpl_begin_scope(cpl);
        block(cpl);
        luluCpl_end_scope(cpl);
        break;
    case TK_PRINT:
        luluLex_next_token(ls);
        print_statement(cpl);
        break;
    default:
        luluLex_error_lookahead(ls, "Expected a statement");
        break;
    }
}

// 1}}} ------------------------------------------------------------------------

// DECLARATIONS ----------------------------------------------------------- {{{1

// Declare a local variable by initializing and adding it to the current scope.
// Intern a local variable name. Analogous to `parseVariable()` in the book.
static void parse_local(Compiler *cpl)
{
    Lexer *ls = cpl->lexer;
    Token *id = &ls->consumed;
    luluCpl_init_local(cpl);
    luluCpl_identifier_constant(cpl, id); // We don't need the index here.
}

/**
 * @brief   For Lua this only matters for local variables. Analogous to
 *          `varDeclaration()` in the book.
 *
 * @details vardecl     ::= 'local' identlist ['=' exprlist] ';'
 *          identlist   ::= identifier [',' identifier]*
 *          exprlist    ::= expression [',' expression]*
 *
 * @note    We don't emit `OP_SETLOCAL`, since we only need to push the value to
 *          the stack without popping it. We already keep track of info like the
 *          variable name.
 */
static void declare_locals(Compiler *cpl)
{
    Lexer *ls     = cpl->lexer;
    int    idents = 0;
    int    exprs  = 0;

    do {
        luluLex_expect_token(ls, TK_IDENT, NULL);
        parse_local(cpl);
        idents += 1;
    } while (luluLex_match_token(ls, TK_COMMA));

    if (luluLex_match_token(ls, TK_ASSIGN))
        exprs = parse_exprlist(cpl);
    adjust_exprlist(cpl, idents, exprs);
    luluCpl_define_locals(cpl, idents);
}

void declaration(Compiler *cpl)
{
    Lexer *ls = cpl->lexer;
    switch (ls->lookahead.type) {
    case TK_LOCAL:
        luluLex_next_token(ls);
        declare_locals(cpl);
        break;
    default:
        statement(cpl);
        break;
    }
    // Lua allows 1 semicolon to terminate statements, but no more.
    luluLex_match_token(ls, TK_SEMICOL);
}

// 1}}} ------------------------------------------------------------------------

// PARSE RULES ------------------------------------------------------------ {{{1

// Assumes the first token is ALWAYS a prefix expression with 0 or more infix
// expressions following it.
static void parse_precedence(Compiler *cpl, Precedence prec)
{
    Lexer     *ls = cpl->lexer;
    Token     *tk = &ls->lookahead; // NOTE: Is updated as lexer moves along!
    ParseRule *pr = get_parserule(tk->type);

    if (pr->prefixfn == NULL) {
        luluLex_error_consumed(ls, "Expected a prefix expression");
        return;
    }
    luluLex_next_token(ls);
    pr->prefixfn(cpl);

    for (;;) {
        pr = get_parserule(tk->type);
        // If we can't further compile the token to our right, end the loop.
        if (!(prec <= pr->precedence))
            break;
        luluLex_next_token(ls);
        pr->infixfn(cpl);
    }

    // This function can never consume the `=` token.
    if (luluLex_match_token(ls, TK_ASSIGN)) {
        luluLex_error_lookahead(ls, "Invalid assignment target");
    }
}

static ParseRule PARSERULES_LOOKUP[] = {
    // TOKEN           PREFIXFN     INFIXFN     PRECEDENCE
    [TK_AND]        = {NULL,        NULL,       PREC_AND},
    [TK_BREAK]      = {NULL,        NULL,       PREC_NONE},
    [TK_DO]         = {NULL,        NULL,       PREC_NONE},
    [TK_ELSE]       = {NULL,        NULL,       PREC_NONE},
    [TK_ELSEIF]     = {NULL,        NULL,       PREC_NONE},
    [TK_END]        = {NULL,        NULL,       PREC_NONE},
    [TK_FALSE]      = {&literal,    NULL,       PREC_NONE},
    [TK_FOR]        = {NULL,        NULL,       PREC_NONE},
    [TK_FUNCTION]   = {NULL,        NULL,       PREC_NONE},
    [TK_IF]         = {NULL,        NULL,       PREC_NONE},
    [TK_IN]         = {NULL,        NULL,       PREC_NONE},
    [TK_LOCAL]      = {NULL,        NULL,       PREC_NONE},
    [TK_NIL]        = {&literal,    NULL,       PREC_NONE},
    [TK_NOT]        = {&unary,      NULL,       PREC_NONE},
    [TK_OR]         = {NULL,        NULL,       PREC_NONE},
    [TK_PRINT]      = {NULL,        NULL,       PREC_NONE},
    [TK_RETURN]     = {NULL,        NULL,       PREC_NONE},
    [TK_THEN]       = {NULL,        NULL,       PREC_NONE},
    [TK_TRUE]       = {&literal,    NULL,       PREC_NONE},
    [TK_WHILE]      = {NULL,        NULL,       PREC_NONE},

    [TK_LPAREN]     = {&grouping,   NULL,       PREC_NONE},
    [TK_RPAREN]     = {NULL,        NULL,       PREC_NONE},
    [TK_LBRACKET]   = {NULL,        NULL,       PREC_NONE},
    [TK_RBRACKET]   = {NULL,        NULL,       PREC_NONE},
    [TK_LCURLY]     = {&table,      NULL,       PREC_NONE},
    [TK_RCURLY]     = {NULL,        NULL,       PREC_NONE},

    [TK_COMMA]      = {NULL,        NULL,       PREC_NONE},
    [TK_SEMICOL]    = {NULL,        NULL,       PREC_NONE},
    [TK_VARARG]     = {NULL,        NULL,       PREC_NONE},
    [TK_CONCAT]     = {NULL,        &concat,    PREC_CONCAT},
    [TK_PERIOD]     = {NULL,        NULL,       PREC_NONE},
    [TK_POUND]      = {&unary,      NULL,       PREC_UNARY},

    [TK_PLUS]       = {NULL,        &binary,    PREC_TERMINAL},
    [TK_DASH]       = {&unary,      &binary,    PREC_TERMINAL},
    [TK_STAR]       = {NULL,        &binary,    PREC_FACTOR},
    [TK_SLASH]      = {NULL,        &binary,    PREC_FACTOR},
    [TK_PERCENT]    = {NULL,        &binary,    PREC_FACTOR},
    [TK_CARET]      = {NULL,        &binary,    PREC_POW},

    [TK_ASSIGN]     = {NULL,        NULL,       PREC_NONE},
    [TK_EQ]         = {NULL,        &binary,    PREC_EQUALITY},
    [TK_NEQ]        = {NULL,        &binary,    PREC_EQUALITY},
    [TK_GT]         = {NULL,        &binary,    PREC_COMPARISON},
    [TK_GE]         = {NULL,        &binary,    PREC_COMPARISON},
    [TK_LT]         = {NULL,        &binary,    PREC_COMPARISON},
    [TK_LE]         = {NULL,        &binary,    PREC_COMPARISON},

    [TK_IDENT]      = {&variable,   NULL,       PREC_NONE},
    [TK_STRING]     = {&string,     NULL,       PREC_NONE},
    [TK_NUMBER]     = {&number,     NULL,       PREC_NONE},
    [TK_ERROR]      = {NULL,        NULL,       PREC_NONE},
    [TK_EOF]        = {NULL,        NULL,       PREC_NONE},
};

static ParseRule *get_parserule(TkType key)
{
    return &PARSERULES_LOOKUP[key];
}

// 1}}} ------------------------------------------------------------------------
