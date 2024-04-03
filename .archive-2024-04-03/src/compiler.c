#include "compiler.h"
#include "object.h"
#include "opcodes.h"
#include "vm.h"

#define xtostring(x)    #x
#define stringify(x)    xtostring(x)
#define logformat(s)    __FILE__ ":" stringify(__LINE__) ": " s

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

void init_compiler(Compiler *self, lua_VM *vm, Lexer *lex) {
    self->lex = lex;
    self->vm  = vm;
    self->freereg = 0;
}

// Later on when function definitions are involved this will get complicated.
static Chunk *current_chunk(Compiler *self) {
    return self->vm->chunk;
}

// Append the given `instruction` to the current compiling chunk.
static void emit_instruction(Compiler *self, Instruction instruction) {
    write_chunk(current_chunk(self), instruction, self->lex->token.line);
}

/**
 * @param results   Number of expected return values for the current compiling
 *                  function.
 */
static void emit_return(Compiler *self, int results) {
    emit_instruction(self, CREATE_ABC(OP_RETURN, 0, results, 0));
}

/**
 * @brief   Since we store constant values' indexes in `Bx`, we must check that
 *          the newly stored constant value's index does not exceed the maximum
 *          allowable value for the `Bx` register.
 *
 * @note    Assumes that type `int` can fit `MAXARG_Bx` and above.
 */
static int make_constant(Compiler *self, const TValue *value) {
    int index = add_constant(current_chunk(self), value);
    if (index >= MAXARG_Bx) {
        lexerror_consumed(self->lex, "Too many constants in one chunk.");
        return 0;
    }
    return index;
}

// TODO: How to manage emitting to register A here?
static int emit_constant(Compiler *self, const TValue *value) {
    int index = make_constant(self, value);
    emit_instruction(self, CREATE_ABx(OP_CONSTANT, self->freereg++, index));
    return index;
}

/**
 * @note    When compiling Lua, 1 return 'value' is always emitted even if it
 *          gets ignored by a user-specified `return`.
 */
static void end_compiler(Compiler *self) {
    emit_return(self, 1);
#ifdef DEBUG_PRINT_CODE
    disassemble_chunk(current_chunk(self));
#endif
}

static void init_exprdesc(ExprDesc *self, ExprKind kind, int info) {
    self->tag = kind;
    self->args.info = info;
}

static void expression(Compiler *self, ExprDesc *expr);
static const ParseRule *get_rule(TkType type);
static void parse_precedence(Compiler *self, ExprDesc *expr, Precedence prec);

// --- INFIX EXPRESSIONS -------------------------------------------------- {{{1

static OpCode get_binop(TkType optype) {
    switch (optype) {
    case TK_PLUS:       return OP_ADD;
    case TK_DASH:       return OP_SUB;
    case TK_STAR:       return OP_MUL;
    case TK_SLASH:      return OP_DIV;
    case TK_PERCENT:    return OP_MOD;
    case TK_CARET:      return OP_POW;
    default:
        // Should be unreachable in normal circumstances...
        return cast(OpCode, 0);
    }
}

/**
 * TODO:    How to manage register arguments here?
 *
 * @note    Assumes the leading token for this expression, i.e a number literal
 *          has been consumed and that the entire left hand side expressions has
 *          been compiled.
 */
static void binary(Compiler *self, ExprDesc *expr) {
    Lexer *lex = self->lex;
    TkType optype  = lex->token.type;
    ExprDesc rhs = {0};
    Precedence prec = get_rule(optype)->prec;
    
    // Only enforce right-associativity for exponentiation.
    if (optype == TK_CARET) {
        prec++;
    }
    parse_precedence(self, &rhs, prec);
    
    // NOTE: This assumes that both arguments are just registers...
    int ra  = expr->args.info;
    int rkb = ra;
    int rkc = rhs.args.info;
    Instruction inst = CREATE_ABC(get_binop(optype), ra, rkb, rkc);
    if (expr->tag == EXPR_CONSTANT) {
        SETARG_B(inst, RKASK(rkb));
    }
    if (rhs.tag == EXPR_CONSTANT) {
        SETARG_C(inst, RKASK(rkc));
    }
    emit_instruction(self, inst);
}

// 1}}} ------------------------------------------------------------------------

// --- PREFIX EXPRESSIONS ------------------------------------------------- {{{1

/**
 * @brief   When encountering (hopefully) balanced `'('` and `')'`, recursively
 *          compile everything in between them.
 *
 * @note    Assumes that the first `'('` has been consumed.
 *          Calls `expression()` which may recurse back to this.
 */
static void grouping(Compiler *self, ExprDesc *expr) {
    expression(self, expr);
    consume_token(self->lex, TK_RPAREN, "Expected ')' after expression.");
}

/**
 * @note    Assumes we just consumed a `TK_NUMBER` token and that it is now in 
 *          `self->lex->token`.
 *          See: https://www.lua.org/source/5.1/lparser.c.html#simpleexp
 */
static void number(Compiler *self, ExprDesc *expr) {
    Lexer *lex = self->lex;
    const Token *token = &lex->token;
    const char *refptr = token->start + token->len;
    char *endptr;
    lua_Number n = lua_str2num(token->start, &endptr);
    if (endptr != refptr) {
        lexerror_consumed(lex, "Malformed number");
    } else {
        // int index = emit_constant(self, &makenumber(n));
        int index = make_constant(self, &makenumber(n));
        init_exprdesc(expr, EXPR_CONSTANT, index);
    }
}

/**
 * @brief   By themselves unary operators do not push values, but their compiled
 *          operands will like push something to the first free register. This
 *          register will be modified in-place.
 *
 * @note    Assumes an unary expression token has been consumed, e.g. `'-'`
 *          as `TK_DASH`. We also assume the sole argument to an unary operator
 *          is the most recently use register.
 */
static void unary(Compiler *self, ExprDesc *expr) {
    Lexer *lex = self->lex;
    TkType op  = lex->token.type; // Keep in stack frame memory for recursion.

    // Compile any and all operands/operations that are of a higher or equal
    // precedence. We use the same precedence to enforce right-associativity.
    parse_precedence(self, expr, PREC_UNARY);

    // Index of most recently used register to be modified in-place.
    int arg = self->freereg - 1;

    // Emit the operator instructions.
    switch (op) {
    case TK_DASH:
        emit_instruction(self, CREATE_ABC(OP_UNM, arg, arg, 0));
        break;
    default:
        // Unreachable and should not happen.
        break;
    }
}

// 1}}} ------------------------------------------------------------------------

// --- PRECEDENCE LOOKUP TABLE -------------------------------------------- {{{1

static const ParseRule rules[] = {
    /* --- RESERVED WORDS ------------------------------------------------- {{{2
    [TOKEN TYPE]   := {PREFIX FN    INFIX FN    PRECEDENCE} */
    [TK_AND]        = {NULL,        NULL,       PREC_NONE},
    [TK_BREAK]      = {NULL,        NULL,       PREC_NONE},
    [TK_DO]         = {NULL,        NULL,       PREC_NONE},
    [TK_ELSE]       = {NULL,        NULL,       PREC_NONE},
    [TK_ELSEIF]     = {NULL,        NULL,       PREC_NONE},
    [TK_END]        = {NULL,        NULL,       PREC_NONE},
    [TK_FALSE]      = {NULL,        NULL,       PREC_NONE},
    [TK_FOR]        = {NULL,        NULL,       PREC_NONE},
    [TK_FUNCTION]   = {NULL,        NULL,       PREC_NONE},
    [TK_IF]         = {NULL,        NULL,       PREC_NONE},
    [TK_IN]         = {NULL,        NULL,       PREC_NONE},
    [TK_LOCAL]      = {NULL,        NULL,       PREC_NONE},
    [TK_NIL]        = {NULL,        NULL,       PREC_NONE},
    [TK_NOT]        = {NULL,        NULL,       PREC_NONE},
    [TK_OR]         = {NULL,        NULL,       PREC_NONE},
    [TK_RETURN]     = {NULL,        NULL,       PREC_NONE},
    [TK_THEN]       = {NULL,        NULL,       PREC_NONE},
    [TK_TRUE]       = {NULL,        NULL,       PREC_NONE},
    [TK_WHILE]      = {NULL,        NULL,       PREC_NONE},
    // 2}}} --------------------------------------------------------------------

    // --- ARITHMETIC OPERATORS ------------------------------------------- {{{2
    [TK_PLUS]       = {NULL,        binary,     PREC_TERMINAL},
    [TK_DASH]       = {unary,       binary,     PREC_TERMINAL},
    [TK_STAR]       = {NULL,        binary,     PREC_FACTOR},
    [TK_SLASH]      = {NULL,        binary,     PREC_FACTOR},
    [TK_PERCENT]    = {NULL,        binary,     PREC_FACTOR},
    [TK_CARET]      = {NULL,        binary,     PREC_FACTOR},
    // 2}}} --------------------------------------------------------------------

    // --- RELATIONAL OPERATORS ------------------------------------------- {{{2
    [TK_EQ]         = {NULL,        NULL,       PREC_EQUALITY},
    [TK_NEQ]        = {NULL,        NULL,       PREC_EQUALITY},
    [TK_GT]         = {NULL,        NULL,       PREC_COMPARISON},
    [TK_GE]         = {NULL,        NULL,       PREC_COMPARISON},
    [TK_LT]         = {NULL,        NULL,       PREC_COMPARISON},
    [TK_LE]         = {NULL,        NULL,       PREC_COMPARISON},
    // 2}}} --------------------------------------------------------------------

    /* --- BALANCED PAIRS ------------------------------------------------- {{{2
    [TOKEN TYPE]   := {PREFIX FN    INFIX FN    PRECEDENCE} */
    [TK_LPAREN]     = {grouping,    NULL,       PREC_NONE},
    [TK_RPAREN]     = {NULL,        NULL,       PREC_NONE},
    [TK_LBRACKET]   = {NULL,        NULL,       PREC_NONE},
    [TK_RBRACKET]   = {NULL,        NULL,       PREC_NONE},
    [TK_LCURLY]     = {NULL,        NULL,       PREC_NONE},
    [TK_RCURLY]     = {NULL,        NULL,       PREC_NONE},
    // 2}}} --------------------------------------------------------------------

    // --- PUNCTUATION MARKS ---------------------------------------------- {{{2
    [TK_ASSIGN]     = {NULL,        NULL,       PREC_ASSIGNMENT},
    [TK_COMMA]      = {NULL,        NULL,       PREC_NONE},
    [TK_SEMICOL]    = {NULL,        NULL,       PREC_NONE},
    [TK_PERIOD]     = {NULL,        NULL,       PREC_NONE},
    [TK_CONCAT]     = {NULL,        NULL,       PREC_NONE},
    [TK_VARARG]     = {NULL,        NULL,       PREC_NONE},
    // 2}}} --------------------------------------------------------------------

    // --- VARIABLY SIZED TOKENS ------------------------------------------ {{{2
    [TK_NUMBER]     = {number,      NULL,       PREC_NONE},
    [TK_NAME]       = {NULL,        NULL,       PREC_NONE},
    [TK_STRING]     = {NULL,        NULL,       PREC_NONE},
    // 2}}} --------------------------------------------------------------------

    [TK_ERROR]      = {NULL,        NULL,       PREC_NONE},
    [TK_EOF]        = {NULL,        NULL,       PREC_NONE},
};

// 1}}} ------------------------------------------------------------------------

static void parse_precedence(Compiler *self, ExprDesc *expr, Precedence prec) {
    Lexer *lex = self->lex;
    next_token(lex);
    ParseFn prefixfn = get_rule(lex->token.type)->prefix;
    if (prefixfn == NULL) {
        lexerror_consumed(lex, "Expected an expression");
        return;
    }
    prefixfn(self, expr);
    
    while (prec <= get_rule(lex->lookahead.type)->prec) {
        next_token(lex);
        ParseFn infixfn = get_rule(lex->token.type)->infix;
        infixfn(self, expr);
    }
}

static const ParseRule *get_rule(TkType type) {
    return &rules[type];
}

static void expression(Compiler *self, ExprDesc *expr) {
    // Disallow assignments outside of dedicated assignment statements.
    parse_precedence(self, expr, PREC_ASSIGNMENT + 1);
}

static void statement(Compiler *self) {
    Lexer *lex = self->lex;
    Token *token = &lex->token;
    ExprDesc expr = {0};
    switch (token->type) {
    case TK_NUMBER:
        // Hack for the meantime in order to push *something* to a register
        number(self, &expr);
        emit_instruction(self, CREATE_ABx(OP_CONSTANT, self->freereg++, expr.args.info));
        next_token(lex);
        break;
    default:
        break;
    }
    expression(self, &expr);
}

bool compile(Compiler *self, const char *input) {
    lua_VM *vm    = self->vm;
    Lexer *lex    = self->lex;
    Chunk *chunk  = vm->chunk;
    init_lex(lex, self, chunk->name, input);
    next_token(lex);
    statement(self);
    consume_token(lex, TK_EOF, "Expected end of expression");
    end_compiler(self);
    return true;
}
