#include "compiler.h"
#include "vm.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

void init_compiler(Compiler *self, Lexer *lexer, VM *vm) {
    self->lexer = lexer;
    self->vm    = vm;
}

static Chunk *current_chunk(Compiler *self) {
    return self->chunk;
}

// CODE GENERATION -------------------------------------------------------- {{{1

static void emit_instruction(Compiler *self, Instruction inst) {
    Lexer *lexer = self->lexer;
    Token *token = &lexer->consumed;
    write_chunk(current_chunk(self), inst, token->line);
}

static void emit_return(Compiler *self) {
    emit_instruction(self, create_iNone(OP_RETURN));
}

static int make_constant(Compiler *self, const TValue *value) {
    Lexer *lexer = self->lexer;
    int index = add_constant(current_chunk(self), value);
    if (index >= MAXARG_Bx) {
        lexerror_at_consumed(lexer, "Too many constants in current chunk");
    }
    return index;
}

static void emit_constant(Compiler *self, const TValue *value) {
    const int index = make_constant(self, value);
    emit_instruction(self, create_iBx(OP_CONSTANT, index));
}

static void end_compiler(Compiler *self) {
    emit_return(self);
#ifdef DEBUG_PRINT_CODE
    disassemble_chunk(current_chunk(self));
#endif
}

// Forward declarations to allow recursive descent parsing.

static void expression(Compiler *self);
static const ParseRule *get_parserule(TkType key);
static void parse_precedence(Compiler *self, Precedence prec);

// INFIX EXPRESSIONS ------------------------------------------------------ {{{2

static OpCode get_binop(TkType optype) {
    switch (optype) {
    case TK_PLUS:    return OP_ADD;
    case TK_DASH:    return OP_SUB;
    case TK_STAR:    return OP_MUL;
    case TK_SLASH:   return OP_DIV;
    case TK_PERCENT: return OP_MOD;
    case TK_CARET:   return OP_POW;
    default:
        assert(false); // Should not happen but just in case
        return OP_RETURN;
    }
}
// Assumes we just consumed a binary operator as a possible infix expression,
// and that the left-hand side has been fully compiled.
static void binary(Compiler *self) {
    Lexer *lexer = self->lexer;
    Token *token = &lexer->consumed;
    TkType optype = token->type;
    OpCode opcode = get_binop(optype);
    const ParseRule *rule = get_parserule(optype);

    // For exponentiation, enforce right-associativity.
    parse_precedence(self, (optype == TK_CARET) ? rule->prec : rule->prec + 1);
    emit_instruction(self, create_iNone(opcode));
}

// 2}}} ------------------------------------------------------------------------

// PREFIX EXPRESSIONS ----------------------------------------------------- {{{2

// Assumes we just consumed a '(' as a possible prefix expression: a grouping.
static void grouping(Compiler *self) {
    Lexer *lexer = self->lexer;
    expression(self);
    consume_token(lexer, TK_RPAREN, "Expected ')' after expression");
}

// Assumes we just consumed a possible number literal as a prefix expression.
static void number(Compiler *self) {
    Lexer *lexer = self->lexer;
    Token *token = &lexer->consumed;
    char *endptr;
    Number value = cstr_tonumber(token->start, &endptr);

    // If this is true, strtod failed to convert the entire token/lexeme.
    if (endptr != (token->start + token->len)) {
        lexerror_at_consumed(lexer, "Malformed number");
    } else {
        emit_constant(self, &make_number(value));
    }
}

// Assumes a leading operator has been consumed as prefix expression, e.g. '-'.
static void unary(Compiler *self) {
    Lexer *lexer  = self->lexer;
    Token *token  = &lexer->consumed;
    TkType optype = token->type; // Save due to recursion when compiling.

    // Recursively compiles until we hit something with a lower precedence.
    parse_precedence(self, PREC_UNARY);

    // Emit the operator instruction. Since we're a stack-based VM we can assume
    // that the operand is on the top of the stack so no operands are baked into
    // the instructions themselves.
    switch (optype) {
    case TK_DASH:
        emit_instruction(self, create_iNone(OP_UNM));
        break;
    default:
        // Should be unreachable.
        return;
    }
}

// 2}}} ------------------------------------------------------------------------

// 1}}} ------------------------------------------------------------------------

// PARSING PRECEDENCE ----------------------------------------------------- {{{1

static const ParseRule parserules[] = {
    // TOKEN           PREFIXFN     INFIXFN     PRECEDENCE
    [TK_AND]        = {NULL,        NULL,       PREC_AND},
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

    [TK_LPAREN]     = {grouping,    NULL,       PREC_NONE},
    [TK_RPAREN]     = {NULL,        NULL,       PREC_NONE},
    [TK_LBRACKET]   = {NULL,        NULL,       PREC_NONE},
    [TK_RBRACKET]   = {NULL,        NULL,       PREC_NONE},
    [TK_LCURLY]     = {NULL,        NULL,       PREC_NONE},
    [TK_RCURLY]     = {NULL,        NULL,       PREC_NONE},

    [TK_COMMA]      = {NULL,        NULL,       PREC_NONE},
    [TK_SEMICOL]    = {NULL,        NULL,       PREC_NONE},
    [TK_VARARG]     = {NULL,        NULL,       PREC_NONE},
    [TK_CONCAT]     = {NULL,        NULL,       PREC_NONE},
    [TK_PERIOD]     = {NULL,        NULL,       PREC_NONE},

    [TK_PLUS]       = {NULL,        binary,     PREC_TERMINAL},
    [TK_DASH]       = {unary,       binary,     PREC_TERMINAL},
    [TK_STAR]       = {NULL,        binary,     PREC_FACTOR},
    [TK_SLASH]      = {NULL,        binary,     PREC_FACTOR},
    [TK_PERCENT]    = {NULL,        binary,     PREC_FACTOR},
    [TK_CARET]      = {NULL,        binary,     PREC_FACTOR},

    [TK_ASSIGN]     = {NULL,        NULL,       PREC_NONE},
    [TK_EQ]         = {NULL,        NULL,       PREC_NONE},
    [TK_NEQ]        = {NULL,        NULL,       PREC_NONE},
    [TK_GT]         = {NULL,        NULL,       PREC_NONE},
    [TK_GE]         = {NULL,        NULL,       PREC_NONE},
    [TK_LT]         = {NULL,        NULL,       PREC_NONE},
    [TK_LE]         = {NULL,        NULL,       PREC_NONE},

    [TK_IDENT]      = {NULL,        NULL,       PREC_NONE},
    [TK_STRING]     = {NULL,        NULL,       PREC_NONE},
    [TK_NUMBER]     = {number,      NULL,       PREC_NONE},
    [TK_ERROR]      = {NULL,        NULL,       PREC_NONE},
    [TK_EOF]        = {NULL,        NULL,       PREC_NONE},
};

// We disallow nested assignments as in Lua, those must be lone statements.
static void expression(Compiler *self) {
    parse_precedence(self, PREC_ASSIGN + 1);
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

    while (prec <= get_parserule(lexer->token.type)->prec) {
        next_token(lexer);
        ParseFn infixfn = get_parserule(lexer->consumed.type)->infixfn;
        infixfn(self);
    }
}

static const ParseRule *get_parserule(TkType key) {
    return &parserules[key];
}

// 1}}} ------------------------------------------------------------------------

void compile(Compiler *self, const char *input, Chunk *chunk) {
    Lexer *lexer = self->lexer;
    VM *vm       = self->vm;
    self->chunk  = chunk;
    init_lexer(lexer, input, vm, self);
    next_token(lexer);
    expression(self);
    consume_token(lexer, TK_EOF, "Expected end of expression");
    end_compiler(self);
}
