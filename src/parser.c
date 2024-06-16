#include "parser.h"
#include "object.h"
#include "string.h"
#include "table.h"
#include "vm.h"

// Forward declarations to allow recursive descent parsing.
static ParseRule *get_parserule(TkType key);
static void parse_precedence(Compiler *comp, Precedence prec);

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
static void statement(Compiler *comp);

/**
 * @brief   By itself, always results in exactly 1 value being pushed.
 *
 * @note    We don't parse the same precedence as assignment in order to
 *          disallow C-style constructs like `print(x = 13)`, which is usually
 *          intended to be `print(x == 13)`.
 */
static void expression(Compiler *comp);

static int parse_exprlist(Compiler *comp)
{
    Lexer *ls    = comp->lexer;
    int    exprs = 0;
    do {
        expression(comp);
        exprs += 1;
    } while (luluLex_match_token(ls, TK_COMMA));
    return exprs;
}

static void adjust_exprlist(Compiler *comp, int idents, int exprs)
{
    if (exprs == idents)
        return;

    // True: Discard extra expressions. False: Assign nils to remaining idents.
    if (exprs > idents)
        luluComp_emit_oparg1(comp, OP_POP, exprs - idents);
    else
        luluComp_emit_oparg1(comp, OP_NIL, idents - exprs);
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
static void binary(Compiler *comp)
{
    Lexer  *ls   = comp->lexer;
    TkType  type = ls->consumed.type;

    // For exponentiation enforce right-associativity.
    parse_precedence(comp, get_parserule(type)->precedence + (type != TK_CARET));

    // NEQ, GT and GE must be encoded as logical NOT of their counterparts.
    switch (type) {
    case TK_NEQ:
        luluComp_emit_opcode(comp, OP_EQ);
        luluComp_emit_opcode(comp, OP_NOT);
        break;
    case TK_GT:
        luluComp_emit_opcode(comp, OP_LE);
        luluComp_emit_opcode(comp, OP_NOT);
        break;
    case TK_GE:
        luluComp_emit_opcode(comp, OP_LT);
        luluComp_emit_opcode(comp, OP_NOT);
        break;
    default:
        luluComp_emit_opcode(comp, get_binop(type));
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
static void concat(Compiler *comp)
{
    Lexer *ls   = comp->lexer;
    int    argc = 1;
    do {
        if (argc + 1 > MAX_BYTE)
            luluLex_error_consumed(ls, "Too many consecutive concatenations");
        parse_precedence(comp, PREC_CONCAT + 1);
        argc += 1;
    } while (luluLex_match_token(ls, TK_CONCAT));
    luluComp_emit_oparg1(comp, OP_CONCAT, argc);
}

// 2}}} ------------------------------------------------------------------------

// PREFIX ----------------------------------------------------------------- {{{2

static void literal(Compiler *comp)
{
    switch (comp->lexer->consumed.type) {
    case TK_NIL:   luluComp_emit_oparg1(comp, OP_NIL, 1); break;
    case TK_TRUE:  luluComp_emit_opcode(comp, OP_TRUE);   break;
    case TK_FALSE: luluComp_emit_opcode(comp, OP_FALSE);  break;
    default:
        // Should not happen
        break;
    }
}

// Assumes we just consumed a '('.
static void grouping(Compiler *comp)
{
    Lexer *ls = comp->lexer;

    // Hacky to create a new scope but lets us error at too many C-facing calls.
    // See: https://www.lua.org/source/5.1/lparser.c.html#enterlevel
    luluComp_begin_scope(comp);
    expression(comp);
    luluLex_expect_token(ls, TK_RPAREN, NULL);
    luluComp_end_scope(comp);
}

// Assumes the lexer successfully consumed and encoded a number literal.
static void number(Compiler *comp)
{
    Lexer *ls = comp->lexer;
    Value  v  = make_number(ls->number);
    luluComp_emit_constant(comp, &v);
}

static void string(Compiler *comp)
{
    Lexer *ls = comp->lexer;
    Value  v  = make_string(ls->string);
    luluComp_emit_constant(comp, &v);
}

// Assumes we consumed a `'['` or an identifier representing a table field.
static bool parse_field(Compiler *comp, bool assigning)
{
    Lexer *ls = comp->lexer;
    Token *tk = &ls->consumed;
    switch (tk->type) {
    case TK_LBRACKET:
        expression(comp);
        luluLex_expect_token(ls, TK_RBRACKET, NULL);
        break;
    case TK_PERIOD:
        if (assigning) {
            return false;
        }
        luluLex_expect_token(ls, TK_IDENT, NULL); // Fall through
    case TK_IDENT:
        luluComp_emit_identifier(comp, tk);
        break;
    default:
        break;
    }
    if (!assigning) {
        luluComp_emit_opcode(comp, OP_GETTABLE);
    }
    return true;
}

static void resolve_variable(Compiler *comp, const Token *id)
{
    Lexer *ls = comp->lexer;
    luluComp_emit_variable(comp, id);
    while (luluLex_match_token_any(ls, TK_LBRACKET, TK_PERIOD)) {
        parse_field(comp, false);
    }
}

static void parse_ctor(Compiler *comp, int t_idx, int *count)
{
    Lexer *ls = comp->lexer;

    if (luluLex_match_token(ls, TK_IDENT)) {
        Token id    = ls->consumed; // Copy by value as lexer might update.
        int   k_idx = comp->stack_usage;
        if (luluLex_match_token(ls, TK_ASSIGN)) {
            luluComp_emit_identifier(comp, &id);
            expression(comp);
            luluComp_emit_oparg3(comp, OP_SETTABLE, encode_byte3(t_idx, k_idx, 2));
        } else {
            resolve_variable(comp, &id);
            *count += 1;
        }
    } else if (luluLex_match_token(ls, TK_LBRACKET)) {
        int k_idx = comp->stack_usage;

        parse_field(comp, true);
        luluLex_expect_token(ls, TK_ASSIGN, "to assign table field");
        expression(comp);

        // Always pop the key and value assigning in a table constructor.
        luluComp_emit_oparg3(comp, OP_SETTABLE, encode_byte3(t_idx, k_idx, 2));
    } else {
        expression(comp);
        *count += 1;
    }
    luluLex_match_token(ls, TK_COMMA);
}

static void table(Compiler *comp)
{
    Lexer *ls     = comp->lexer;
    int    t_idx  = comp->stack_usage;
    int    total  = 0; // Array length plus hashmap length.
    int    count  = 0; // Array portion length.
    int    offset = luluComp_emit_table(comp);
    while (!luluLex_match_token(ls, TK_RCURLY)) {
        parse_ctor(comp, t_idx, &count);
        total += 1;
    }

    if (count > 0) {
        if (count + 1 > MAX_BYTE3) {
            luluLex_error_consumed(ls, "Too many elements in table constructor");
        }
        luluComp_emit_oparg2(comp, OP_SETARRAY, encode_byte2(t_idx, count));
    }

    if (total > 0) {
        luluComp_patch_table(comp, offset, total);
    }
}

/**
 * @brief   Originally analogous to `namedVariable()` in the book, but with our
 *          current semantics ours is radically different.
 *
 * @note    Past the first lexeme, assigning of variables is not allowed in Lua.
 *          So this function can only ever perform get operations.
 */
static void variable(Compiler *comp)
{
    Lexer *ls = comp->lexer;
    Token *id = &ls->consumed;
    resolve_variable(comp, id);
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
static void unary(Compiler *comp)
{
    Lexer *ls   = comp->lexer;
    TkType type = ls->consumed.type; // Save in stack-frame memory.
    parse_precedence(comp, PREC_UNARY);
    luluComp_emit_opcode(comp, get_unop(type));
}

// 2}}} ------------------------------------------------------------------------

static void expression(Compiler *comp)
{
    parse_precedence(comp, PREC_ASSIGN + 1);
}

// 1}}} ------------------------------------------------------------------------

// STATEMENTS ------------------------------------------------------------- {{{1

// ASSIGNMENTS ------------------------------------------------------------ {{{2

static void init_assignment(Assignment *comp, Assignment *prev, AssignType type)
{
    comp->prev = prev;
    comp->type = type;
    comp->arg  = -1;
}

static void set_assignment(Assignment *comp, AssignType type, int arg)
{
    comp->type = type;
    comp->arg  = arg;
}

static int count_assignments(Assignment *comp)
{
    Assignment *node = comp;
    int         count = 0;
    while (node != NULL) {
        node   = node->prev;
        count += 1;
    }
    return count;
}

static void emit_assignment_tail(Compiler *comp, Assignment *list)
{
    if (list == NULL)
        return;

    switch (list->type) {
    case ASSIGN_GLOBAL:
        luluComp_emit_oparg3(comp, OP_SETGLOBAL, list->arg);
        break;
    case ASSIGN_LOCAL:
        luluComp_emit_oparg1(comp, OP_SETLOCAL, list->arg);
        break;
    case ASSIGN_TABLE:
        // For assignments we assume the key is always right after the table.
        luluComp_emit_oparg3(comp, OP_SETTABLE, encode_byte3(list->arg, list->arg + 1, 1));
        break;
    }
    emit_assignment_tail(comp, list->prev);

    // After recursion, clean up stack if we emitted table fields as they cannot
    // be implicitly popped due to SETTABLE being a VAR_DELTA pop.
    if (list->type == ASSIGN_TABLE)
        luluComp_emit_oparg1(comp, OP_POP, 2);
}

static void emit_assignment(Compiler *comp, Assignment *list)
{
    int idents = count_assignments(list);
    int exprs  = parse_exprlist(comp);
    adjust_exprlist(comp, idents, exprs);
    emit_assignment_tail(comp, list);
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
static void discharge_assignment(Compiler *comp, Assignment *list, bool isfield)
{
    Lexer *ls = comp->lexer;

    // Get the table itself if this is the first token in an lvalue, or if this
    // is part of a recursive call we need GETTABLE to push a subtable.
    switch (list->type) {
    case ASSIGN_GLOBAL:
        luluComp_emit_oparg3(comp, OP_GETGLOBAL, list->arg);
        break;
    case ASSIGN_LOCAL:
        luluComp_emit_oparg1(comp, OP_GETLOCAL,  list->arg);
        break;
    case ASSIGN_TABLE:
        luluComp_emit_opcode(comp, OP_GETTABLE);
        break;
    }

    // Emit the key immediately. In recursive calls we'll emit another GETTABLE,
    // otherwise we'll know the key is +1 after the table when we emit SETTABLE.
    if (isfield) {
        Token *id = &ls->consumed;
        luluLex_expect_token(ls, TK_IDENT, "after '.'");
        luluComp_emit_identifier(comp, id);
    } else {
        expression(comp);
        luluLex_expect_token(ls, TK_RBRACKET, NULL);
    }

    // stack_usage - 0 is top, -1 is the key we emitted and -2 is the table.
    set_assignment(list, ASSIGN_TABLE, comp->stack_usage - 2);
}

// Assumes we consumed an identifier as the first element of a statement.
static void identifier_statement(Compiler *comp, Assignment *list)
{
    Lexer *ls = comp->lexer;
    Token *id = &ls->consumed;

    if (list->type != ASSIGN_TABLE) {
        int  arg     = luluComp_resolve_local(comp, id);
        bool islocal = (arg != -1);
        if (!islocal) {
            arg = luluComp_identifier_constant(comp, id);
        }
        set_assignment(list, islocal ? ASSIGN_LOCAL : ASSIGN_GLOBAL, arg);
    }

    switch (ls->lookahead.type) {
    case TK_PERIOD: // Fall through
    case TK_LBRACKET:
        // Emit the table up to this point then mark `list` as a table.
        luluLex_next_token(ls);
        discharge_assignment(comp, list, ls->consumed.type == TK_PERIOD);
        identifier_statement(comp, list);
        break;
    case TK_ASSIGN:
        luluLex_next_token(ls);
        emit_assignment(comp, list);
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
        identifier_statement(comp, &next);
        break;
    }
    default:
        luluLex_error_consumed(ls, "Unexpected token in identifier statement");
    }
}

// OTHER ------------------------------------------------------------------ {{{3

// Assumes we just consumed the `print` keyword and are now ready to compile a
// stream of expressions to act as arguments.
static void print_statement(Compiler *comp)
{
    Lexer *ls   = comp->lexer;
    bool   open = luluLex_match_token(ls, TK_LPAREN);
    int    argc = parse_exprlist(comp);
    if (open)
        luluLex_expect_token(ls, TK_RPAREN, NULL);
    luluComp_emit_oparg1(comp, OP_PRINT, argc);
}

static void block(Compiler *comp)
{
    Lexer *ls = comp->lexer;
    while (!luluLex_check_token_any(ls, TK_END, TK_EOF)) {
        declaration(comp);
    }
    luluLex_expect_token(ls, TK_END, "after block");
}

// 3}}} ------------------------------------------------------------------------

// 2}}} ------------------------------------------------------------------------

static void statement(Compiler *comp)
{
    Lexer *ls = comp->lexer;
    switch (ls->lookahead.type) {
    case TK_IDENT: {
        Assignment list;
        init_assignment(&list, NULL, ASSIGN_GLOBAL); // Ensure no garbage.
        luluLex_next_token(ls);
        identifier_statement(comp, &list);
        break;
    }
    case TK_DO:
        luluLex_next_token(ls);
        luluComp_begin_scope(comp);
        block(comp);
        luluComp_end_scope(comp);
        break;
    case TK_PRINT:
        luluLex_next_token(ls);
        print_statement(comp);
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
static void parse_local(Compiler *comp)
{
    Lexer *ls = comp->lexer;
    Token *id = &ls->consumed;
    luluComp_init_local(comp);
    luluComp_identifier_constant(comp, id); // We don't need the index here.
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
static void declare_locals(Compiler *comp)
{
    Lexer *ls     = comp->lexer;
    int    idents = 0;
    int    exprs  = 0;

    do {
        luluLex_expect_token(ls, TK_IDENT, NULL);
        parse_local(comp);
        idents += 1;
    } while (luluLex_match_token(ls, TK_COMMA));

    if (luluLex_match_token(ls, TK_ASSIGN))
        exprs = parse_exprlist(comp);
    adjust_exprlist(comp, idents, exprs);
    luluComp_define_locals(comp, idents);
}

void declaration(Compiler *comp)
{
    Lexer *ls = comp->lexer;
    switch (ls->lookahead.type) {
    case TK_LOCAL:
        luluLex_next_token(ls);
        declare_locals(comp);
        break;
    default:
        statement(comp);
        break;
    }
    // Lua allows 1 semicolon to terminate statements, but no more.
    luluLex_match_token(ls, TK_SEMICOL);
}

// 1}}} ------------------------------------------------------------------------

// PARSE RULES ------------------------------------------------------------ {{{1

// Assumes the first token is ALWAYS a prefix expression with 0 or more infix
// expressions following it.
static void parse_precedence(Compiler *comp, Precedence prec)
{
    Lexer     *ls = comp->lexer;
    Token     *tk = &ls->lookahead; // NOTE: Is updated as lexer moves along!
    ParseRule *pr = get_parserule(tk->type);

    if (pr->prefixfn == NULL) {
        luluLex_error_consumed(ls, "Expected a prefix expression");
        return;
    }
    luluLex_next_token(ls);
    pr->prefixfn(comp);

    for (;;) {
        pr = get_parserule(tk->type);
        // If we can't further compile the token to our right, end the loop.
        if (!(prec <= pr->precedence))
            break;
        luluLex_next_token(ls);
        pr->infixfn(comp);
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
