#include "parser.h"
#include "object.h"
#include "vm.h"

// Forward declarations to allow recursive descent parsing.
static ParseRule *get_parserule(TkType key);
static void parse_precedence(Compiler *self, Precedence prec);

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
static void statement(Compiler *self);

/**
 * @brief   By itself, always results in exactly 1 value being pushed.
 *
 * @note    We don't parse the same precedence as assignment in order to
 *          disallow C-style constructs like `print(x = 13)`, which is usually
 *          intended to be `print(x == 13)`.
 */
static void expression(Compiler *self);

static int parse_exprlist(Compiler *self)
{
    Lexer *lexer = self->lexer;
    int    exprs = 0;
    do {
        expression(self);
        exprs += 1;
    } while (match_token(lexer, TK_COMMA));
    return exprs;
}

static void adjust_exprlist(Compiler *self, int idents, int exprs)
{
    if (exprs == idents) {
        return;
    }

    if (exprs > idents) {
        // Discard extra expressions.
        emit_oparg1(self, OP_POP, exprs - idents);
    } else {
        // Assign nils to remaining identifiers.
        emit_oparg1(self, OP_NIL, idents - exprs);
    }
}

// EXPRESSIONS ------------------------------------------------------------ {{{1

// INFIX ------------------------------------------------------------------ {{{2

static OpCode get_binop(TkType optype)
{
    switch (optype) {
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
static void binary(Compiler *self)
{
    Lexer  *lexer  = self->lexer;
    TkType  optype = lexer->consumed.type;

    // For exponentiation enforce right-associativity.
    parse_precedence(self, get_parserule(optype)->prec + (optype != TK_CARET));

    // NEQ, GT and GE must be encoded as logical NOT of their counterparts.
    switch (optype) {
    case TK_NEQ:
        emit_opcode(self, OP_EQ);
        emit_opcode(self, OP_NOT);
        break;
    case TK_GT:
        emit_opcode(self, OP_LE);
        emit_opcode(self, OP_NOT);
        break;
    case TK_GE:
        emit_opcode(self, OP_LT);
        emit_opcode(self, OP_NOT);
        break;
    default:
        emit_opcode(self, get_binop(optype));
        break;
    }
}

// Assumes we just consumed a `..` and the first argument has been compiled.
static void concat(Compiler *self)
{
    Lexer *lexer = self->lexer;
    int argc = 1;
    do {
        /* Although right associative, we don't recursively compile concat
        expressions in the same grouping. We can do this iteratively, which
        is marginally better than constantly allocating stack frames for
        recursive calls. */
        parse_precedence(self, PREC_CONCAT + 1);
        argc += 1;
    } while (match_token(lexer, TK_CONCAT));
    emit_oparg1(self, OP_CONCAT, argc);
}

// 2}}} ------------------------------------------------------------------------

// PREFIX ----------------------------------------------------------------- {{{2

static void literal(Compiler *self)
{
    switch (self->lexer->consumed.type) {
    case TK_NIL:   emit_oparg1(self, OP_NIL, 1); break;
    case TK_TRUE:  emit_opcode(self, OP_TRUE);   break;
    case TK_FALSE: emit_opcode(self, OP_FALSE);  break;
    default:
        // Should not happen
        break;
    }
}

// Assumes we just consumed a '('.
static void grouping(Compiler *self)
{
    Lexer *lexer = self->lexer;

    // Hacky to create a new scope but lets us error at too many C-facing calls.
    // See: https://www.lua.org/source/5.1/lparser.c.html#enterlevel
    begin_scope(self);
    expression(self);
    expect_token(lexer, TK_RPAREN, NULL);
    end_scope(self);
}

// Assumes the lexer successfully consumed and encoded a number literal.
static void number(Compiler *self)
{
    Lexer *lexer   = self->lexer;
    Value wrapper = make_number(lexer->number);
    emit_constant(self, &wrapper);
}

static void string(Compiler *self)
{
    Lexer *lexer   = self->lexer;
    Value wrapper = make_string(lexer->string);
    emit_constant(self, &wrapper);
}

static void emit_index(Compiler *self, int *index)
{
    Value wrapper = make_number(*index);
    emit_constant(self, &wrapper);
    *index += 1;
}

// Assumes we consumed a `'['` or an identifier representing a table field.
static bool parse_field(Compiler *self, bool assigning)
{
    Lexer *lexer = self->lexer;
    switch (lexer->consumed.type) {
    case TK_LBRACKET:
        expression(self);
        expect_token(lexer, TK_RBRACKET, NULL);
        break;
    case TK_PERIOD:
        if (assigning) {
            return false;
        }
        expect_token(lexer, TK_IDENT, NULL); // Fall through
    case TK_IDENT:
        emit_identifier(self);
        break;
    default:
        break;
    }
    if (!assigning) {
        emit_opcode(self, OP_GETTABLE);
    }
    return true;
}

static void parse_ctor(Compiler *self, int *index)
{
    Lexer *lexer  = self->lexer;
    Byte   offset = self->stack.usage - 1; // Absolute index of table itself.

    // TODO: Allow identifiers as implied array elements
    if (match_token_any(lexer, TK_LBRACKET, TK_IDENT)) {
        parse_field(self, true);
        expect_token(lexer, TK_ASSIGN, "to assign table field");
    } else {
        emit_index(self, index);
    }
    expression(self);

    // Always pop the key and value for each field in a table constructor.
    emit_oparg2(self, OP_SETTABLE, decode_byte2(offset, 2));
    match_token(lexer, TK_COMMA);
}

static void table(Compiler *self)
{
    Lexer *lexer = self->lexer;
    Table *table = new_table(&self->vm->alloc);
    Value value = make_table(table);
    int    index = 1;

    // Always emit a getop for the table itself, especially when assigning
    emit_constant(self, &value);
    while (!match_token(lexer, TK_RCURLY)) {
        parse_ctor(self, &index);
    }
}

/**
 * @brief   Originally analogous to `namedVariable()` in the book, but with our
 *          current semantics ours is radically different.
 *
 * @note    Past the first lexeme, assigning of variables is not allowed in Lua.
 *          So this function can only ever perform get operations.
 */
static void variable(Compiler *self)
{
    Lexer *lexer = self->lexer;
    Token *ident = &lexer->consumed;
    emit_variable(self, ident);

    // Have 1+ table fields?
    while (match_token_any(lexer, TK_LBRACKET, TK_PERIOD)) {
        parse_field(self, false);
    }
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
static void unary(Compiler *self)
{
    Lexer *lexer = self->lexer;
    TkType type  = lexer->consumed.type; // Save in stack-frame memory.
    parse_precedence(self, PREC_UNARY);
    emit_opcode(self, get_unop(type));
}

// 2}}} ------------------------------------------------------------------------

static void expression(Compiler *self)
{
    parse_precedence(self, PREC_ASSIGN + 1);
}

// 1}}} ------------------------------------------------------------------------

// STATEMENTS ------------------------------------------------------------- {{{1

// ASSIGNMENTS ------------------------------------------------------------ {{{2

static void init_assignment(Assignment *self, Assignment *prev, AssignType type)
{
    self->prev = prev;
    self->type = type;
    self->arg  = -1;
}

static void set_assignment(Assignment *self, AssignType type, int arg)
{
    self->type = type;
    self->arg  = arg;
}

static int count_assignments(Assignment *self)
{
    int count = 0;
    while (self != NULL) {
        count += 1;
        self   = self->prev;
    }
    return count;
}

static void emit_assignment_tail(Compiler *self, Assignment *list)
{
    if (list == NULL) {
        return;
    }
    switch (list->type) {
    case ASSIGN_GLOBAL:
        emit_oparg3(self, OP_SETGLOBAL, list->arg);
        break;
    case ASSIGN_LOCAL:
        emit_oparg1(self, OP_SETLOCAL, list->arg);
        break;
    case ASSIGN_TABLE:
        // For arg A use absolute index of table, for arg B pop only the value.
        emit_oparg2(self, OP_SETTABLE, decode_byte2(list->arg, 1));
        break;
    }
    emit_assignment_tail(self, list->prev);

    // After recursion, clean up stack if we emitted table fields as they cannot
    // be implicitly popped due to SETTABLE being a VAR_DELTA pop.
    if (list->type == ASSIGN_TABLE) {
        emit_oparg1(self, OP_POP, 2);
    }
}

static void emit_assignment(Compiler *self, Assignment *list)
{
    int idents = count_assignments(list);
    int exprs  = parse_exprlist(self);
    adjust_exprlist(self, idents, exprs);
    emit_assignment_tail(self, list);
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
static void discharge_assignment(Compiler *self, Assignment *list, TkType type)
{
    Lexer *lexer = self->lexer;
    next_token(lexer);

    // Emit the table itself if this is the first token in an lvalue, or if this
    // is part of a recursive call we need GETTABLE to push the subtable.
    switch (list->type) {
    case ASSIGN_GLOBAL:
        emit_oparg3(self, OP_GETGLOBAL, list->arg);
        break;
    case ASSIGN_LOCAL:
        emit_oparg1(self, OP_GETLOCAL, list->arg);
        break;
    case ASSIGN_TABLE:
        emit_opcode(self, OP_GETTABLE);
        break;
    }

    // Discharge the field we just consumed instead of storing it in the list.
    switch (type) {
    case TK_LBRACKET:
        expression(self);
        expect_token(lexer, TK_RBRACKET, NULL);
        break;
    case TK_PERIOD:
        expect_token(lexer, TK_IDENT, "after '.'");
        emit_identifier(self);
        break;
    default:
        // Should be unreachable
        break;
    }

    // 0 is top, -1 is key, -2 is table.
    set_assignment(list, ASSIGN_TABLE, self->stack.usage - 2);
}

// Assumes we consumed an identifier as the first element of a statement.
static void identifier_statement(Compiler *self, Assignment *elem)
{
    Lexer *lexer = self->lexer;
    Token *ident = &lexer->consumed;

    if (elem->type != ASSIGN_TABLE) {
        int  arg     = resolve_local(self, ident);
        bool islocal = (arg != -1);
        if (!islocal) {
            arg = identifier_constant(self, ident);
        }
        set_assignment(elem, islocal ? ASSIGN_LOCAL : ASSIGN_GLOBAL, arg);
    }

    switch (lexer->lookahead.type) {
    case TK_PERIOD: // Fall through
    case TK_LBRACKET:
        // Emit the table up to this point then mark `elem` as a table.
        discharge_assignment(self, elem, lexer->lookahead.type);
        identifier_statement(self, elem);
        break;
    case TK_ASSIGN:
        next_token(lexer);
        emit_assignment(self, elem);
        break;
    case TK_LPAREN:
        lexerror_at_consumed(lexer, "Function calls not yet implemented");
        break;
    case TK_COMMA: {
        // Recursive call so chain elements together.
        Assignment next;
        init_assignment(&next, elem, ASSIGN_GLOBAL);
        next_token(lexer);
        expect_token(lexer, TK_IDENT, "after ','");
        identifier_statement(self, &next);
        break;
    }
    default:
        lexerror_at_consumed(lexer, "Unexpected token in identifier statement");
    }
}

// OTHER ------------------------------------------------------------------ {{{3

// Assumes we just consumed the `print` keyword and are now ready to compile a
// stream of expressions to act as arguments.
static void print_statement(Compiler *self)
{
    Lexer *lexer = self->lexer;
    bool   open  = match_token(lexer, TK_LPAREN);
    int    argc  = parse_exprlist(self);
    if (open) {
        expect_token(lexer, TK_RPAREN, NULL);
    }
    emit_oparg1(self, OP_PRINT, argc);
}

static void block(Compiler *self)
{
    Lexer *lexer = self->lexer;
    while (!check_token_any(lexer, TK_END, TK_EOF)) {
        declaration(self);
    }
    expect_token(lexer, TK_END, "after block");
}

// 3}}} ------------------------------------------------------------------------

// 2}}} ------------------------------------------------------------------------

static void statement(Compiler *self)
{
    Lexer *lexer = self->lexer;
    switch (lexer->lookahead.type) {
    case TK_IDENT: {
        Assignment ident;
        init_assignment(&ident, NULL, ASSIGN_GLOBAL); // Ensure no garbage.
        next_token(lexer);
        identifier_statement(self, &ident);
        break;
    }
    case TK_DO:
        next_token(lexer);
        begin_scope(self);
        block(self);
        end_scope(self);
        break;
    case TK_PRINT:
        next_token(lexer);
        print_statement(self);
        break;
    default:
        lexerror_at_lookahead(lexer, "Expected a statement");
        break;
    }
}

// 1}}} ------------------------------------------------------------------------

// DECLARATIONS ----------------------------------------------------------- {{{1

// Declare a local variable by initializing and adding it to the current scope.
// Intern a local variable name. Analogous to `parseVariable()` in the book.
static void parse_local(Compiler *self)
{
    Lexer *lexer = self->lexer;
    Token *token = &lexer->consumed;
    init_local(self);
    identifier_constant(self, token); // We don't need the index here.
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
static void declare_locals(Compiler *self)
{
    Lexer *lexer  = self->lexer;
    int    idents = 0;
    int    exprs  = 0;

    do {
        expect_token(lexer, TK_IDENT, NULL);
        parse_local(self);
        idents += 1;
    } while (match_token(lexer, TK_COMMA));

    if (match_token(lexer, TK_ASSIGN)) {
        exprs = parse_exprlist(self);
    }
    adjust_exprlist(self, idents, exprs);
    define_locals(self, idents);
}

void declaration(Compiler *self)
{
    Lexer *lexer = self->lexer;
    switch (lexer->lookahead.type) {
    case TK_LOCAL:
        next_token(lexer);
        declare_locals(self);
        break;
    default:
        statement(self);
        break;
    }
    // Lua allows 1 semicolon to terminate statements, but no more.
    match_token(lexer, TK_SEMICOL);
}

// 1}}} ------------------------------------------------------------------------

// PARSE RULES ------------------------------------------------------------ {{{1

// Assumes the first token is ALWAYS a prefix expression with 0 or more infix
// expressions following it.
static void parse_precedence(Compiler *self, Precedence prec)
{
    Lexer *lexer = self->lexer;
    next_token(lexer);
    ParseFn prefixfn = get_parserule(lexer->consumed.type)->prefixfn;
    if (prefixfn == NULL) {
        lexerror_at_consumed(lexer, "Expected a prefix expression");
        return;
    }
    prefixfn(self);

    // Is the token to our right something we can further compile?
    while (prec <= get_parserule(lexer->lookahead.type)->prec) {
        next_token(lexer);
        get_parserule(lexer->consumed.type)->infixfn(self);
    }

    // This function can never consume the `=` token.
    if (match_token(lexer, TK_ASSIGN)) {
        lexerror_at_lookahead(lexer, "Invalid assignment target");
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
