#include "debug.h"
#include "parser.h"
#include "object.h"
#include "vm.h"

// Forward declarations to allow recursive descent parsing.
static ParseRule *get_parserule(TkType key);
static void parse_precedence(Compiler *self, Precedence prec);

static void adjust_stackinfo(Compiler *self, int count) {
    Lexer *lexer = self->lexer;
    self->stackusage += count;
    if (self->stackusage + 1 > MAX_STACK) {
        lexerror_at_consumed(lexer, "Function uses too many stack slots");
    }
    if (self->stackusage > self->stacktotal) {
        self->stacktotal = self->stackusage;
    }
}

static bool identifiers_equal(const Token *a, const Token *b) {
    if (a->len != b->len) {
        return false;
    }
    return cstr_equal(a->start, b->start, a->len);
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
    Token *name  = &lexer->consumed;

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

typedef void (*IdentFn)(Compiler *self, int count, void *context);

// Generic function for `local` declaration/s or assignment list.
static int parse_identlist(Compiler *self, IdentFn identfn, void *context) {
    Lexer *lexer = self->lexer;
    int idents = 0;
    do {
        consume_token(lexer, TK_IDENT, "Expected an identifier");
        identfn(self, idents, context);
        idents++;
    } while (match_token(lexer, TK_COMMA));
    return idents;
}

// Please pass only negative values, similar to `vm.c:poke_top()`.
static void mark_initialized(Compiler *self, int offset) {
    self->locals[self->localcount + offset].depth = self->scopedepth;
}

// Analogous to `defineVariable()` in the book, but for a comma-separated list
// in the form `'local' identifier [, identifier]* [';']`.
static void define_locals(Compiler *self, int count) {
    // We considered "defined" local variables to be ready for reading/writing.
    for (int i = count; i > 0; i--) {
        mark_initialized(self, -i);
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

    // For exponentiation enforce right-associativity.
    parse_precedence(self, get_parserule(optype)->prec + (optype != TK_CARET));

    // NEQ, GT and GE must be encoded as logical NOT of their counterparts.
    switch (optype) {
    case TK_NEQ: emit_oparg1(self, OP_EQ, OP_NOT);    break;
    case TK_GT:  emit_oparg1(self, OP_LE, OP_NOT);    break;
    case TK_GE:  emit_oparg1(self, OP_LT, OP_NOT);    break;
    default:     emit_byte(self,  get_binop(optype)); break;
    }

    adjust_stackinfo(self, -2 + 1); // Pop 2 operands, push 1 result.
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

    emit_oparg3(self, OP_CONCAT, argc);
    adjust_stackinfo(self, -argc + 1); // Pop all operands but push 1 result.
}

// 1}}} ------------------------------------------------------------------------

// PREFIX EXPRESSIONS ----------------------------------------------------- {{{1

static void literal(Compiler *self) {
    switch (self->lexer->consumed.type) {
    case TK_NIL:    emit_oparg1(self, OP_NIL, 1); break;
    case TK_TRUE:   emit_byte(self, OP_TRUE);     break;
    case TK_FALSE:  emit_byte(self, OP_FALSE);    break;
    default:
        // Should not happen
        break;
    }
    adjust_stackinfo(self, 1);
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
        adjust_stackinfo(self, 1);
    }
}

static void string(Compiler *self) {
    Lexer *lexer = self->lexer;
    Token *token = &lexer->consumed;

    // Left +1 to skip left quote, len -2 to get offset of last non-quote.
    TString *ts = copy_string(self->vm, token->start + 1, token->len - 2);
    TValue wrapper = make_string(ts);
    emit_constant(self, &wrapper);
    adjust_stackinfo(self, 1);
}

// Originally analogous to `namedVariable()` in the book, but with our current
// semantics ours is radically different.
static void emit_getop(Compiler *self, const Token *name) {
    int operand  = resolve_local(self, name);
    bool islocal = (operand != -1);

    // Getops will always push.
    adjust_stackinfo(self, 1);

    // Global vs. local operands have different sizes.
    if (islocal) {
        emit_oparg1(self, OP_GETLOCAL, operand);
    } else {
        emit_oparg3(self, OP_GETGLOBAL, identifier_constant(self, name));
    }
}

// Past the first lexeme, assigning of variables is not allowed in Lua.
// So this function can only ever perform get operations.
static void variable(Compiler *self) {
    Lexer *lexer = self->lexer;
    emit_getop(self, &lexer->consumed);

    // Have `t[...]`, try to resolve the key for the table access
    if (match_token(lexer, TK_LBRACKET)) {
        expression(self);
        consume_token(lexer, TK_RBRACKET, "Expected ']' to close '['");
        emit_byte(self, OP_GETTABLE);
    }
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
    emit_byte(self, get_unop(optype));
}

static void assign_table(Compiler *self, int *index) {
    Lexer *lexer = self->lexer;

    // Keys should always have a net stack effect of +1.
    if (match_token(lexer, TK_LBRACKET)) {
        // Explicit hash key: `["Monday"] = ...` or an expression thereof
        expression(self);
        consume_token(lexer, TK_RBRACKET, "Expected ']' to close '['");
        consume_token(lexer, TK_ASSIGN, "Expected '=' to assign table field");
    } else {
        // Implicit index: `t = {13}`. is equivalent to `t = {[1] = 13}`
        TValue value = make_number(*index++);
        emit_constant(self, &value);
    }
    expression(self);
    emit_byte(self, OP_SETTABLE);
}

// Table constructor is a prefix expression. Assumes we consumed a `'{'` token.
static void table_ctor(Compiler *self) {
    Lexer *lexer = self->lexer;
    Table *table = new_table(&self->vm->alloc);
    TValue value = make_table(table);

    // Emit table BEFORE setting any potential fields.
    emit_constant(self, &value);

    // Not an empty constructor?
    if (!check_token(lexer, TK_RCURLY)) {
        // Lua arrays start with 1 so for consistency I'll do the same.
        int index = 1;
        do {
            assign_table(self, &index);
        } while (match_token(lexer, TK_COMMA));
    }
    consume_token(lexer, TK_RCURLY, "Expected '}' after table constructor");
    adjust_stackinfo(self, +1);
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
    [TK_LCURLY]     = {table_ctor,  NULL,       PREC_NONE},
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
        int extra = exprs - idents;
        emit_oparg1(self, OP_POP, extra);
        adjust_stackinfo(self, -extra);
    } else {
        // Assign nils to remaining identifiers.
        int extra = idents - exprs;
        emit_oparg1(self, OP_NIL, extra);
        adjust_stackinfo(self, extra);
    }
}

// Declare a local variable by initializing and adding it to the current scope.
// Intern a local variable name. Analogous to `parseVariable()` in the book.
static void parse_local(Compiler *self, int count, void *context) {
    unused2(count, context);
    Lexer *lexer = self->lexer;
    Token *token = &lexer->consumed;
    init_local(self);
    identifier_constant(self, token); // We don't need the index into Kst.
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
 *          the stack without popping it. We already keep track of info like
 *          variable name.
 */
static void declare_locals(Compiler *self) {
    Lexer *lexer = self->lexer;
    int    count = parse_identlist(self, parse_local, NULL);
    if (match_token(lexer, TK_ASSIGN)) {
        adjust_exprlist(self, count, parse_exprlist(self));
    } else {
        // Stack usage is otherwise modified by each call to `expression()` and
        // potentially `adjust_exprlist()`.
        emit_oparg1(self, OP_NIL, count);
        adjust_stackinfo(self, count);
    }
    define_locals(self, count);
}

// Slightly ugly but I can't think of a better way to resolve compound assigns.
typedef struct Assign Assign;
typedef struct ExprInfo ExprInfo;

struct ExprInfo {
    int addr; // Bytecode "address", a.k.a. index into the bytecode array.
    int size; // Total number of bytes used for the entire expression.
};

struct Assign {
    ExprInfo assignee;
    ExprInfo value;  // Assigner.
};

// Setop for local or global. Assumes `list` is `MAX_MULTI` elements long.
static void parse_variable(Compiler *self, int count, void *context) {
    Assign *list    = cast(Assign*, context);
    Lexer  *lexer   = self->lexer;
    Token  *name    = &lexer->consumed;
    int     operand = resolve_local(self, name);
    bool    islocal = (operand != -1);
    if (count + 1 > MAX_MULTI) {
        lexerror_at_consumed(lexer,
            "More than " stringify(MAX_MULTI) " variables in assignment list");
    }
    // Index of `OP_SET*` instruction.
    list[count].assignee.addr = self->chunk->len;
    if (islocal) {
        emit_oparg1(self, OP_SETLOCAL, operand);
        list[count].assignee.size = 1;
    } else {
        emit_oparg3(self, OP_SETGLOBAL, identifier_constant(self, name));
        list[count].assignee.size = 3;
    }
}

/**
 * @brief   Assumes we are about to consume the first identifier in an
 *          assignment list.
 *
 * @details varlist         ::= identlist '=' exprlist
 *          identlist       ::= identifier [',' identifier]*
 *          exprlist        ::= expression [',' expression]*
 *
 * @note    In Lua, a varlist as the first statement is either assumed to be
 *          1 or more assignments, or a function call. You cannot have comma
 *          separated function calls.
 */
static void assign_variables(Compiler *self) {
    Assign list[MAX_MULTI];
    Lexer *lexer  = self->lexer;
    int    idents = parse_identlist(self, parse_variable, list);
    consume_token(lexer, TK_ASSIGN, "Expected '='");

    int exprs = 0;
    do {
        int offset = self->chunk->len;
        expression(self);
        // Don't modify the list if too many expressions vs. assignments.
        if (exprs > idents) {
            continue;
        }
        list[exprs].value.addr = offset;
        list[exprs].value.size = self->chunk->len - offset - 1;
        exprs++;
    } while (match_token(lexer, TK_COMMA));
    adjust_exprlist(self, idents, exprs);

    // Too few expressions, remanining assignments are mass-assigned nil
    // so we just need to get the index of the `OP_NIL` instruction.
    if (exprs < idents) {
        int opnil = self->chunk->len - 1 - 1;
        for (int i = exprs; i < idents; i++) {
            list[i].value.addr = opnil;
            list[i].value.size = 2;
        }
    }

    // Swap the bytecode at these points
    disassemble_chunk(self->chunk);
    for (int i = 0; i < idents; i++) {
        // In-place swap bytecode of assignee and assigning
        printf("assignee 0x%04x (size: %i) => value 0x%04x (size: %i)\n",
                list[i].assignee.addr,
                list[i].assignee.size,
                list[i].value.addr,
                list[i].value.size);
    }
    lexerror_at_consumed(lexer, "Multiple assignment is WIP again");

    adjust_stackinfo(self, -idents);
}

// Assumes we just consumed the `print` keyword and are now ready to compile a
// stream of expressions to act as arguments.
static void print_statement(Compiler *self) {
    Lexer *lexer = self->lexer;
    bool   open  = match_token(lexer, TK_LPAREN); // Hack for function "call"
    int    argc  = parse_exprlist(self);
    if (open) {
        consume_token(lexer, TK_RPAREN, "Expected ')' to close '('");
    }
    emit_oparg1(self, OP_PRINT, argc);
    adjust_stackinfo(self, -argc);
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
    switch (lexer->token.type) {
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
    adjust_stackinfo(self, -1);
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
    switch (lexer->token.type) {
    case TK_IDENT:
        // Do not consume the identifier, we need it at the top.
        assign_variables(self);
        break;
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
    while (prec <= get_parserule(lexer->token.type)->prec) {
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
