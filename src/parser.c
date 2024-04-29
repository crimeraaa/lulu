#include "parser.h"
#include "object.h"
#include "vm.h"

// Forward declarations to allow recursive descent parsing.
static ParseRule *get_parserule(TkType key);
static void parse_precedence(Compiler *self, Precedence prec);

static bool identifiers_equal(const Token *a, const Token *b) {
    return (a->len == b->len) && cstr_equal(a->start, b->start, a->len);
}

// Returns index of a local variable or -1 if assumed to be global.
static int resolve_local(Compiler *self, const Token *name) {
    for (int i = self->localcount - 1; i >= 0; i--) {
        const Local *local = &self->locals[i];
        // If using itself in initializer, continue to resolve outward.
        if (local->depth != -1 && identifiers_equal(name, &local->name)) {
            return i;
        }
    }
    return -1;
}

// Initializes the current top of the locals array.
// Returns index of newly initialized local into the locals array.
static void add_local(Compiler *self, const Token *name) {
    if (self->localcount + 1 > MAX_LOCALS) {
        lexerror_at_consumed(self->lexer,
            "More than " stringify(MAX_LOCALS) " local variables reached");
    }
    Local *local = &self->locals[self->localcount++];
    local->name  = *name;
    local->depth = -1;
}

// Analogous to `declareVariable()` in the book, but only for Lua locals.
// Assumes we just consumed a local variable identifier token.
static void init_local(Compiler *self) {
    Lexer *lexer = self->lexer;
    const Token *name = &lexer->consumed;

    // Detect variable shadowing in the same scope.
    for (int i = self->localcount - 1; i >= 0; i--) {
        const Local *local = &self->locals[i];
        // Have we hit an outer scope?
        if (local->depth != -1 && local->depth < self->scopedepth) {
            break;
        }
        if (identifiers_equal(name, &local->name)) {
            lexerror_at_consumed(lexer, "Shadowing of local variable");
        }
    }
    add_local(self, name);
}

// Analogous to `defineVariable()` in the book, but for a comma-separated list
// form `'local' identifier [, identifier]* [';']`.
// We considered "defined" local variables to be ready for reading/writing.
static void define_locals(Compiler *self, int count) {
    int limit = self->localcount;
    for (int i = count; i > 0; i--) {
        self->locals[limit - i].depth = self->scopedepth;
    }
}

/**
 * @note    In the book, Robert uses `parsePrecedence(PREC_ASSIGNMENT)` but
 *          doing that will allow nested assignments as in C. For Lua, we have
 *          to use 1 precedence higher to disallow them.
 */
static void expression(Compiler *self);
static void statement(Compiler *self);

// INFIX EXPRESSIONS ------------------------------------------------------ {{{1

static OpCode get_binop(TkType optype) {
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
static void binary(Compiler *self) {
    Lexer *lexer  = self->lexer;
    Token *token  = &lexer->consumed;
    TkType optype = token->type;
    ParseRule *rule = get_parserule(optype);

    // For exponentiation enforce right-associativity.
    parse_precedence(self, rule->prec + (optype != TK_CARET));

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
static void concat(Compiler *self) {
    Lexer *lexer = self->lexer;
    int argc = 1;
    do {
        // Although right associative, we don't recursively compile concat
        // expressions in the same grouping.
        parse_precedence(self, PREC_CONCAT + 1);
        argc++;
    } while (match_token(lexer, TK_CONCAT));
    emit_oparg1(self, OP_CONCAT, argc);
}

// 1}}} ------------------------------------------------------------------------

// PREFIX EXPRESSIONS ----------------------------------------------------- {{{1

static void literal(Compiler *self) {
    switch (self->lexer->consumed.type) {
    case TK_NIL: {
        Chunk *chunk = current_chunk(self);
        int    len   = chunk->len;
        // Minor optimization: combine consecutive OP_NIL's
        if (len >= 2 && chunk->code[len - 2] == OP_NIL) {
            chunk->code[len - 1] += 1;
        } else {
            emit_oparg1(self, OP_NIL, 1);
        }
        break;
    }
    case TK_TRUE:  emit_opcode(self, OP_TRUE);   break;
    case TK_FALSE: emit_opcode(self, OP_FALSE);  break;
    default:
        // Should not happen
        break;
    }
}

// Assumes we just consumed a '('.
static void grouping(Compiler *self) {
    Lexer *lexer = self->lexer;
    expression(self);
    consume_token(lexer, TK_RPAREN, "Expected ')' after expression");
}

// Assumes we just consumed a possible number literal.
static void number(Compiler *self) {
    Lexer *lexer = self->lexer;
    Token *token = &lexer->consumed;
    char *endptr; // Populated by below function call
    Number value = cstr_tonumber(token->start, &endptr);

    // If this is true, strtod failed to convert the entire token/lexeme.
    if (endptr != (token->start + token->len)) {
        lexerror_at_consumed(lexer, "Malformed number");
    } else {
        TValue wrapper = make_number(value);
        emit_constant(self, &wrapper);
    }
}

static void string(Compiler *self) {
    Lexer *lexer = self->lexer;
    Token *token = &lexer->consumed;

    // Left +1 to skip left quote, len -2 to get offset of last non-quote.
    TString *ts    = copy_string(self->vm, token->start + 1, token->len - 2);
    TValue wrapper = make_string(ts);
    emit_constant(self, &wrapper);
}

// Assumes we consumed a `'['` or an identifier representing a table field.
static void parse_field(Compiler *self) {
    Lexer *lexer = self->lexer;
    Token *token = &lexer->consumed;
    switch (token->type) {
    case TK_LBRACKET:
        expression(self);
        consume_token(lexer, TK_RBRACKET, "Expected ']' to close '['");
        break;
    case TK_IDENT:
        emit_oparg3(self, OP_CONSTANT, identifier_constant(self, token));
        break;
    default:
        break;
    }
}

// Table setops always assume table is at Top[-1].
static void parse_ctor(Compiler *self, int *index) {
    Lexer *lexer = self->lexer;
    if (match_token_any(lexer, TK_LBRACKET, TK_IDENT)) {
        parse_field(self);
        consume_token(lexer, TK_ASSIGN, "Expected '=' to assign table field");
    } else {
        TValue wrapper = make_number(*index);
        emit_constant(self, &wrapper);
        (*index)++; // Different from `*index++` due to C operator precedence.
    }
    expression(self);
    emit_opcode(self, OP_SETTABLE);
    match_token(lexer, TK_COMMA);
}

// Assumes we just consumed a `'{'`.
// TODO: Fix stack info adjustments, it reports wrong values here.
static void table(Compiler *self) {
    Lexer *lexer = self->lexer;
    Table *table = new_table(&self->vm->alloc);
    TValue value = make_table(table);
    int    index = 1;

    // Always emit a getop for the table itself, especially when assigning
    emit_constant(self, &value);
    while (!match_token(lexer, TK_RCURLY)) {
        parse_ctor(self, &index);
    }
}

// Originally analogous to `namedVariable()` in the book, but with our current
// semantics ours is radically different.
static void emit_getop(Compiler *self, const Token *name) {
    Lexer *lexer   = self->lexer;
    int    operand = resolve_local(self, name);
    bool   islocal = (operand != -1);


    // Global vs. local operands have different sizes.
    if (islocal) {
        emit_oparg1(self, OP_GETLOCAL, operand);
    } else {
        emit_oparg3(self, OP_GETGLOBAL, identifier_constant(self, name));
    }

    // Have 1+ table fields?
    while (match_token_any(lexer, TK_LBRACKET, TK_PERIOD)) {
        if (lexer->consumed.type == TK_PERIOD) {
            consume_token(lexer, TK_IDENT, "Expected an identifier");
        }
        parse_field(self);
        emit_opcode(self, OP_GETTABLE);
    }
}

// Past the first lexeme, assigning of variables is not allowed in Lua.
// So this function can only ever perform get operations.
static void variable(Compiler *self) {
    Lexer *lexer = self->lexer;
    emit_getop(self, &lexer->consumed);
}

static OpCode get_unop(TkType type) {
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
static void unary(Compiler *self) {
    Lexer *lexer  = self->lexer;
    Token *token  = &lexer->consumed;
    TkType optype = token->type; // Save due to recursion when compiling.

    // Recursively compiles until we hit something with a lower precedence.
    parse_precedence(self, PREC_UNARY);
    emit_opcode(self, get_unop(optype));
}

// 1}}} ------------------------------------------------------------------------

// PARSING PRECEDENCE ----------------------------------------------------- {{{1

static ParseRule PARSERULES_LOOKUP[] = {
    // TOKEN           PREFIXFN     INFIXFN     PRECEDENCE
    [TK_AND]        = {NULL,        NULL,       PREC_AND},
    [TK_BREAK]      = {NULL,        NULL,       PREC_NONE},
    [TK_DO]         = {NULL,        NULL,       PREC_NONE},
    [TK_ELSE]       = {NULL,        NULL,       PREC_NONE},
    [TK_ELSEIF]     = {NULL,        NULL,       PREC_NONE},
    [TK_END]        = {NULL,        NULL,       PREC_NONE},
    [TK_FALSE]      = {literal,     NULL,       PREC_NONE},
    [TK_FOR]        = {NULL,        NULL,       PREC_NONE},
    [TK_FUNCTION]   = {NULL,        NULL,       PREC_NONE},
    [TK_IF]         = {NULL,        NULL,       PREC_NONE},
    [TK_IN]         = {NULL,        NULL,       PREC_NONE},
    [TK_LOCAL]      = {NULL,        NULL,       PREC_NONE},
    [TK_NIL]        = {literal,     NULL,       PREC_NONE},
    [TK_NOT]        = {unary,       NULL,       PREC_NONE},
    [TK_OR]         = {NULL,        NULL,       PREC_NONE},
    [TK_PRINT]      = {NULL,        NULL,       PREC_NONE},
    [TK_RETURN]     = {NULL,        NULL,       PREC_NONE},
    [TK_THEN]       = {NULL,        NULL,       PREC_NONE},
    [TK_TRUE]       = {literal,     NULL,       PREC_NONE},
    [TK_WHILE]      = {NULL,        NULL,       PREC_NONE},

    [TK_LPAREN]     = {grouping,    NULL,       PREC_NONE},
    [TK_RPAREN]     = {NULL,        NULL,       PREC_NONE},
    [TK_LBRACKET]   = {NULL,        NULL,       PREC_NONE},
    [TK_RBRACKET]   = {NULL,        NULL,       PREC_NONE},
    [TK_LCURLY]     = {table,       NULL,       PREC_NONE},
    [TK_RCURLY]     = {NULL,        NULL,       PREC_NONE},

    [TK_COMMA]      = {NULL,        NULL,       PREC_NONE},
    [TK_SEMICOL]    = {NULL,        NULL,       PREC_NONE},
    [TK_VARARG]     = {NULL,        NULL,       PREC_NONE},
    [TK_CONCAT]     = {NULL,        concat,     PREC_CONCAT},
    [TK_PERIOD]     = {NULL,        NULL,       PREC_NONE},
    [TK_POUND]      = {unary,       NULL,       PREC_UNARY},

    [TK_PLUS]       = {NULL,        binary,     PREC_TERMINAL},
    [TK_DASH]       = {unary,       binary,     PREC_TERMINAL},
    [TK_STAR]       = {NULL,        binary,     PREC_FACTOR},
    [TK_SLASH]      = {NULL,        binary,     PREC_FACTOR},
    [TK_PERCENT]    = {NULL,        binary,     PREC_FACTOR},
    [TK_CARET]      = {NULL,        binary,     PREC_POW},

    [TK_ASSIGN]     = {NULL,        NULL,       PREC_NONE},
    [TK_EQ]         = {NULL,        binary,     PREC_EQUALITY},
    [TK_NEQ]        = {NULL,        binary,     PREC_EQUALITY},
    [TK_GT]         = {NULL,        binary,     PREC_COMPARISON},
    [TK_GE]         = {NULL,        binary,     PREC_COMPARISON},
    [TK_LT]         = {NULL,        binary,     PREC_COMPARISON},
    [TK_LE]         = {NULL,        binary,     PREC_COMPARISON},

    [TK_IDENT]      = {variable,    NULL,       PREC_NONE},
    [TK_STRING]     = {string,      NULL,       PREC_NONE},
    [TK_NUMBER]     = {number,      NULL,       PREC_NONE},
    [TK_ERROR]      = {NULL,        NULL,       PREC_NONE},
    [TK_EOF]        = {NULL,        NULL,       PREC_NONE},
};

static void expression(Compiler *self) {
    parse_precedence(self, PREC_ASSIGN + 1);
}

static void block(Compiler *self) {
    Lexer *lexer = self->lexer;
    while (!check_token_any(lexer, TK_END, TK_EOF)) {
        declaration(self);
    }
    consume_token(lexer, TK_END, "Expected 'end' after block");
}

static int parse_exprlist(Compiler *self) {
    Lexer *lexer = self->lexer;
    int exprs = 0;
    do {
        expression(self);
        exprs++;
    } while (match_token(lexer, TK_COMMA));
    return exprs;
}

static void adjust_exprlist(Compiler *self, int idents, int exprs) {
    if (exprs == idents) {
        return;
    }

    if (exprs > idents) {
        // Discard extra expressions.
        emit_oparg1(self, OP_POP, exprs - idents);
    } else {
        // Assignment nils to remaining identifiers.
        emit_oparg1(self, OP_NIL, idents - exprs);
    }
}

// Declare a local variable by initializing and adding it to the current scope.
// Intern a local variable name. Analogous to `parseVariable()` in the book.
static void parse_local(Compiler *self) {
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
static void declare_locals(Compiler *self) {
    Lexer *lexer = self->lexer;
    int count = 0;
    do {
        consume_token(lexer, TK_IDENT, "Expected an identifier");
        parse_local(self);
        count += 1;
    } while (match_token(lexer, TK_COMMA));

    if (match_token(lexer, TK_ASSIGN)) {
        adjust_exprlist(self, count, parse_exprlist(self));
    } else {
        emit_oparg1(self, OP_NIL, count);
    }
    define_locals(self, count);
}

typedef struct Assignment Assignment;

// These are all mutually exclusive flags.
typedef enum {
    ASSIGN_GLOBAL,
    ASSIGN_LOCAL,
    ASSIGN_FIELD,  // Indexing via dot notation e.g. `io.stdout`.
    ASSIGN_INDEX,  // Indexing via bracket notation e.g. `_G["print"]`.
} AssignType;

struct Assignment {
    Assignment *prev;
    int         arg;
    AssignType  type;
};

// We are dealing with nested table field accesses, e.g. `t.k.v`.
static void emit_gettable(Compiler *self, Assignment *list, int *nest) {
    if (list->prev != NULL && list->prev->type >= ASSIGN_FIELD) {
        emit_opcode(self, OP_GETTABLE);
        if (nest != NULL) {
            *nest += 1;
        }
    }
}

static void emit_fields(Compiler *self, Assignment *list, int *nest) {
    if (list == NULL) {
        return;
    }
    // Recurse in such a way that the previous-most (i.e. oldest) Assignment*
    // has its operations resolved first, mainly an OP_GET(GLOBAL|LOCAL).
    emit_fields(self, list->prev, nest);
    switch (list->type) {
    case ASSIGN_INDEX:
        // Key is implicit thanks to the value pushed by `expression()`.
        // So no need to emit an opcode.
        emit_gettable(self, list, nest);
        break;
    case ASSIGN_FIELD:
        emit_gettable(self, list, nest);
        emit_oparg3(self, OP_CONSTANT, list->arg);
        break;
    case ASSIGN_GLOBAL:
        emit_oparg3(self, OP_GETGLOBAL, list->arg);
        break;
    case ASSIGN_LOCAL:
        emit_oparg1(self, OP_GETLOCAL, list->arg);
        break;
    }
}

static void emit_settable(Compiler *self, Assignment *list) {
    int nest = 0;
    // Need to emit the target table (table + key/s) before the value
    emit_fields(self, list, &nest);
    expression(self);
    emit_opcode(self, OP_SETTABLE);

    // HACK: This is stupid but lets us preserve the stack correctly
    // Because if we had no nested tables, we just need to pop the main one.
    emit_oparg1(self, OP_POP, (nest == 0) ? 1 : nest);
}

static void init_assignment(Assignment *self, Assignment *prev, AssignType type) {
    self->prev = prev;
    self->type = type;
    self->arg  = -1;
}

// Assumes we consumed an identifier as the first element of a statement.
static void identifier_statement(Compiler *self, Assignment *elem) {
    Lexer *lexer = self->lexer;
    Token  ident = lexer->consumed;

    // Is this the first statement and NOT an index using bracket notation?
    // NOTE: Recursive calls should initalize `elem` beforehand.
    if (elem->prev == NULL && elem->type != ASSIGN_INDEX) {
        // Assignment occurs first, is resolved to the lvalue, then comparison.
        bool islocal = (elem->arg = resolve_local(self, &ident)) != -1;
        elem->type   = (islocal) ? ASSIGN_LOCAL : ASSIGN_GLOBAL;
        if (!islocal) {
            elem->arg = identifier_constant(self, &ident);
        }
    }

    switch (lexer->lookahead.type) {
    case TK_PERIOD: {
        Assignment next;

        init_assignment(&next, elem, ASSIGN_FIELD);
        next_token(lexer);
        consume_token(lexer, TK_IDENT, "Expected an identifier after '.'");

        ident    = lexer->consumed;
        next.arg = identifier_constant(self, &ident);
        identifier_statement(self, &next);
        break;
    }
    case TK_LBRACKET: {
        Assignment next;

        // Will emit the table up to this point so we can afford to forget it.
        init_assignment(&next, NULL, ASSIGN_INDEX);
        next_token(lexer);

        // Emit the table up to this point and resolve nested bracket indexes.
        emit_fields(self, elem, NULL);
        if (elem->type == ASSIGN_INDEX) {
            emit_opcode(self, OP_GETTABLE);
        }
        expression(self);
        consume_token(lexer, TK_RBRACKET, "Expected ']' to close '['");
        identifier_statement(self, &next);
        break;
    }
    case TK_ASSIGN:
        next_token(lexer);
        switch (elem->type) {
        case ASSIGN_GLOBAL:
            expression(self);
            emit_oparg3(self, OP_SETGLOBAL, elem->arg);
            break;
        case ASSIGN_LOCAL:
            expression(self);
            emit_oparg1(self, OP_SETLOCAL, elem->arg);
            break;
        case ASSIGN_FIELD:
        case ASSIGN_INDEX:
            emit_settable(self, elem);
            break;
        }
        break;
    case TK_LPAREN:
        lexerror_at_consumed(lexer, "Function calls not yet implemented");
        break;
    case TK_COMMA:
        lexerror_at_consumed(lexer, "Multiple assignment not yet implemented");
        break;
    default:
        lexerror_at_consumed(lexer, "Unexpected token in identifier statement");
    }
}

// Assumes we just consumed the `print` keyword and are now ready to compile a
// stream of expressions to act as arguments.
static void print_statement(Compiler *self) {
    Lexer *lexer = self->lexer;
    bool   open  = match_token(lexer, TK_LPAREN);
    int    argc  = parse_exprlist(self);
    if (open) {
        consume_token(lexer, TK_RPAREN, "Expected ')' to close '('");
    }
    emit_oparg1(self, OP_PRINT, argc);
}

/**
 * @brief   Declarations may have a stack effect/s, like pushing values from
 *          expressions to act as locals variables.
 *
 * @details declaration ::= vardecl ';'
 *                        | statement ';'
 */
void declaration(Compiler *self) {
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

// Expressions produce values, but since statements need to have 0 stack effect
// this will pop whatever it produces.
static void exprstmt(Compiler *self) {
    expression(self);
    emit_oparg1(self, OP_POP, 1);
}

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
static void statement(Compiler *self) {
    Lexer *lexer = self->lexer;
    switch (lexer->lookahead.type) {
    case TK_IDENT: {
        Assignment ident;
        // Use dummy type that's not ASSIGN_INDEX or ASSIGN_FIELD so not garbage
        init_assignment(&ident, NULL, ASSIGN_GLOBAL);
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
        exprstmt(self);
        break;
    }
}

// Assumes the first token is ALWAYS a prefix expression with 0 or more infix
// exprssions following it.
static void parse_precedence(Compiler *self, Precedence prec) {
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
        lexerror_at_token(lexer, "Invalid assignment target");
    }
}

static ParseRule *get_parserule(TkType key) {
    return &PARSERULES_LOOKUP[key];
}

// 1}}} ------------------------------------------------------------------------
