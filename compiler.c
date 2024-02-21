#include "compiler.h"
#include "precedence.h"

static inline void init_parser(LuaParser *self) {
    self->haderror  = false;
    self->panicking = false;
}

void init_compiler(LuaCompiler *self) {
    init_parser(&self->parser);
}

/**
 * III:17.3     Emitting Bytecode
 * 
 * For now, the current chunk is the one that got assigned to the compiler instance
 * when it was created in `interpret_vm()`. Later on this will get more complicated.
 */
static inline LuaChunk *current_chunk(LuaCompiler *self) {
    return &self->chunk;
}

/**
 * III:17.2.1   Handling syntax errors
 * 
 * This is generic function to report errors based on some token and a message.
 * Whatever the case, we set the parser's error state to true.
 */
static void error_at(LuaCompiler *self, const LuaToken *token, const char *message) {
    if (self->parser.panicking) {
        return; // Avoid cascading errors for user's sanity
    }
    self->parser.haderror = true;
    self->parser.panicking = true;
    fprintf(stderr, "[line %i] Error", token->line);
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    fprintf(stderr, ": %s\n", message);
}

/**
 * III:17.2.1   Handling syntax errors
 * 
 * More often than not, we want to report an error at the location of the token
 * we just consumed (that is, it's now the parser's previous token).
 */
static inline void error(LuaCompiler *self, const char *message) {
    error_at(self, &self->parser.previous, message);
}

/**
 * III:17.2.1   Handling syntax errors
 * 
 * If the lexer hands us an error token, we simply report its error message.
 * This is a wrapper around the more generic `error_at()` which can take in any
 * arbitrary error message.
 */
static inline void error_at_current(LuaCompiler *self, const char *message) {
    error_at(self, &self->parser.current, message);
}

/**
 * III:17.2     Parsing Tokens
 * 
 * Assume the compiler should move to the next token. So the parser is set to
 * start a new token. This adjusts state of the compiler's parser and lexer.
 */
static void advance_compiler(LuaCompiler *self) {
    self->parser.previous = self->parser.current;
    
    for (;;) {
        self->parser.current = tokenize(&self->lexer);
        if (self->parser.current.type != TOKEN_ERROR) {
            break;
        }
        // Error tokens already point to an error message thanks to the lexer.
        error_at_current(self, self->parser.current.start);
    }
}

/**
 * III:17.2     Parsing Tokens
 * 
 * We only advance the compiler if the current token matches the expected one.
 * Otherwise, we set the compiler into an error state and report the error.
 */
static void consume_token(LuaCompiler *self, LuaTokenType expected, const char *message) {
    if (self->parser.current.type == expected) {
        advance_compiler(self);
        return;
    }
    error_at_current(self, message);
}

/**
 * III:17.3     Emitting Bytecode
 * 
 * This function simply writes to the compiler's current chunk the given byte,
 * and we log line information based on the consumed token (parser's previous).
 */
static inline void emit_byte(LuaCompiler *self, uint8_t byte) {
    write_chunk(current_chunk(self), byte, self->parser.previous.line);
}

/* Helper because we'll be using this a lot. */
static inline void emit_bytes(LuaCompiler *self, uint8_t i, uint8_t ii) {
    emit_byte(self, i);
    emit_byte(self, ii);
}

/* Helper to emit a 1-byte instruction with a 24-bit operand. */
static inline void
emit_long(LuaCompiler *self, uint8_t i, uint8_t ii, uint8_t iii, uint8_t iv) {
    emit_byte(self, i);
    emit_byte(self, ii);
    emit_byte(self, iii);
    emit_byte(self, iv);
}

/* Helper because it's automatically called by `end_compiler()`. */
static inline void emit_return(LuaCompiler *self) {
    emit_byte(self, OP_RET);
}

/**
 * III:17.4.1   Parsers for tokens
 * 
 * Writing constants is hard work, because we can either use the `OP_CONSTANT`
 * OR the `OP_CONSTANT_LONG`, depending on how many constants are in the current
 * chunk's constants pool.
 */
static inline void emit_constant(LuaCompiler *self, LuaValue value) {
    int index = add_constant(current_chunk(self), value);
    if (index <= MAX_CONSTANTS_SHORT) {
        emit_bytes(self, OP_CONSTANT, (uint8_t)index);
    } else if (index <= MAX_CONSTANTS_LONG) {
        uint8_t hi = (index >> 16) & 0xFF; // mask bits 17-24
        uint8_t mi = (index >> 8)  & 0xFF; // mask bits 9-16
        uint8_t lo = (index)       & 0xFF; // mask bits 1-8
        emit_long(self, OP_CONSTANT_LONG, hi, mi, lo);
    } else {
        error(self, "Too many constants in the current chunk");
    }
}

/**
 * III:17.3     Emitting Bytecode
 * 
 * For now we always emit a return for the compiler's current chunk.
 * This makes it so we don't have to remember to do it as ALL chunks need it.
 */
static inline void end_compiler(LuaCompiler *self) {
    emit_return(self);
}

/**
 * BEGIN:       Forward Declarations
 */

static void compile_expression(LuaCompiler *self);
static void parse_precedence(LuaCompiler *self, LuaPrecedence precedence);

/**
 * END:         Forward Declarations
 */

/**
 * III:17.5     Parsing Infix Expressions
 * 
 * Binary operations are a bit of work since we don't know we have one until we
 * hit one of their operators, e.g. as we're parsing '1 + 2', when we're only at
 * '1' we don't know that it's the left hand side of an addition operation.
 * 
 * Fortunately for us the design of our compiler makes it so that '1' is a constant
 * that was just emitted meaning it's at the top of the stack and we can consider
 * it as our leading number regardless.
 * 
 * Using our (for now hypothetical) lookup table of function pointers, we can
 * associate various parser functions with each token type.
 *
 * e.g. '-' is `TOKEN_DASH` which is associated with the prefix parser function 
 * `unary()` and the infix parser function `binary()`.
 */
void binary(LuaCompiler *self) {
    LuaTokenType optype = self->parser.previous.type;
    const LuaParseRule *rule = get_rule(optype);
    // Compile right hand side, and evaluate it if it has higher precedence operations.
    // We use 1 higher precedence to ensure left-to-right associativity.
    parse_precedence(self, (LuaPrecedence)(rule->precedence + 1));

    switch (optype) {
    case TOKEN_PLUS: emit_byte(self, OP_ADD); break;
    case TOKEN_DASH: emit_byte(self, OP_SUB); break;
    case TOKEN_STAR: emit_byte(self, OP_MUL); break;
    case TOKEN_SLASH: emit_byte(self, OP_DIV); break;
    case TOKEN_CARET: emit_byte(self, OP_POW); break; // TODO: right assoc.
    case TOKEN_PERCENT: emit_byte(self, OP_MOD); break;
    default: return; // Should be unreachable.
    }
}

/**
 * III:17.4.2   Parentheses for grouping
 * 
 * Parse the expression insides of parentheses '(' and ')'. Remember that the
 * parentheses have higher precedence than other unary/binary operators, so we try
 * evaluate them by parsing and compiling their expression.
 * 
 * NOTE:
 * 
 * By themselves, parentehses don't emit any bytecode. Rather it's the
 * order in which we evaluate the expressions contained inside the parentheses
 * that's important.
 */
void grouping(LuaCompiler *self) {
    compile_expression(self);
    consume_token(self, TOKEN_RPAREN, "Expected ')' after grouping expression.");
}

/* Parse a number literal and emit it as a constant. */
void number(LuaCompiler *self) {
    double value = strtod(self->parser.previous.start, NULL);
    emit_constant(self, make_luanumber(value));
}

/**
 * III:17.4.3   Unary negation
 * 
 * Assumes the leading '-' token has been consumed and is the parser's previous
 * token.
 */
void unary(LuaCompiler *self) {
    // Keep in this stackframe's memory so that if we recurse, we evaluate the
    // topmost stack frame (innermosts, higher precedences) first and work our 
    // way down until we reach this particular function call.
    LuaTokenType optype = self->parser.previous.type;
    
    // Compile the unary expression's operand, which may be a number literal,
    // another unary operator, a grouping, etc.
    parse_precedence(self, PREC_UNARY);

    // compile expression may recurse! When that's done, we emit this instruction.
    // Remember that opcodes look at the top of the stack for their operands, so
    // this is why we emit the opcode AFTER the compiled expression.
    switch (optype) {
    case TOKEN_DASH:  emit_byte(self, OP_UNM); break;
    default: return; // Should be unreachable.
    }
}

/**
 * III:17.6.1   Parsing with precedence
 * 
 * By definition, all first tokens (literals, parentheses, variable names) are
 * considered "prefix" expressions. This helps kick off the compiler + parser.
 */
static void parse_precedence(LuaCompiler *self, LuaPrecedence precedence) {
    advance_compiler(self);
    const LuaParseFn prefixfn = get_rule(self->parser.previous.type)->prefix;
    if (prefixfn == NULL) {
        error(self, "Expected an expression.");
        return; // Might end up with NULL infixfn as well
    }
    prefixfn(self);
    
    while (precedence <= get_rule(self->parser.current.type)->precedence) {
        advance_compiler(self);
        const LuaParseFn infixfn = get_rule(self->parser.previous.type)->infix;
        infixfn(self);
    }
}

/**
 * III:17.4     Parsing Prefix Expressions
 * 
 * For now, this is all that's left to finish implementing `compile_bytecode()`.
 * But what do we do?
 * 
 * For now we'll only worry about number literals, parentheses groupings, unary
 * negation and basic arithmetic operations.
 * 
 * III:17.4.1   Parsers for tokens
 * 
 * Let's focus first on single-token expressions. Specifically let's focus on
 * number literals.
 * 
 * III:17.4.3   Unary negation
 * 
 * We call `parse_precedence()` using `PREC_ASSIGNMENT` so we evaluate everything
 * that's stronger than or equal to an assignment. We use `PREC_NONE`, which is
 * lower in the enumerations, to break out of this recursion.
 */
static void compile_expression(LuaCompiler *self) {
    parse_precedence(self, PREC_ASSIGNMENT); 
}

bool compile_bytecode(LuaCompiler *self, const char *source) {
    init_lexer(&self->lexer, source); 
    advance_compiler(self);
    compile_expression(self);
    consume_token(self, TOKEN_EOF, "Expected end of expression.");
    end_compiler(self);
    return !self->parser.haderror;
}
