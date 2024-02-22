#include "compiler.h"
#include "value.h"
#include "object.h"
#include "parserules.h"

static inline void init_parser(Parser *self) {
    self->haderror  = false;
    self->panicking = false;
}

void init_compiler(Compiler *self) {
    init_parser(&self->parser);
}

/**
 * III:17.3     Emitting Bytecode
 * 
 * For now, the current chunk is the one that got assigned to the compiler instance
 * when it was created in `interpret_vm()`. Later on this will get more complicated.
 */
static inline Chunk *current_chunk(Compiler *self) {
    return &self->chunk;
}

/**
 * III:17.2.1   Handling syntax errors
 * 
 * This is generic function to report errors based on some token and a message.
 * Whatever the case, we set the parser's error state to true.
 */
static void error_at(Compiler *self, const Token *token, const char *message) {
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
static inline void error(Compiler *self, const char *message) {
    error_at(self, &self->parser.previous, message);
}

/**
 * III:17.2.1   Handling syntax errors
 * 
 * If the lexer hands us an error token, we simply report its error message.
 * This is a wrapper around the more generic `error_at()` which can take in any
 * arbitrary error message.
 */
static inline void error_at_current(Compiler *self, const char *message) {
    error_at(self, &self->parser.current, message);
}

/**
 * III:17.2     Parsing Tokens
 * 
 * Assume the compiler should move to the next token. So the parser is set to
 * start a new token. This adjusts state of the compiler's parser and lexer.
 */
static void advance_compiler(Compiler *self) {
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
static void consume_token(Compiler *self, TokenType expected, const char *message) {
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
static inline void emit_byte(Compiler *self, uint8_t byte) {
    write_chunk(current_chunk(self), byte, self->parser.previous.line);
}

/* Helper because we'll be using this a lot. */
static inline void emit_bytes(Compiler *self, uint8_t i, uint8_t ii) {
    emit_byte(self, i);
    emit_byte(self, ii);
}

/* Helper to emit a 1-byte instruction with a 24-bit operand. */
static inline void
emit_long(Compiler *self, uint8_t i, uint8_t ii, uint8_t iii, uint8_t iv) {
    emit_byte(self, i);
    emit_byte(self, ii);
    emit_byte(self, iii);
    emit_byte(self, iv);
}

/* Helper because it's automatically called by `end_compiler()`. */
static inline void emit_return(Compiler *self) {
    emit_byte(self, OP_RET);
}

/**
 * III:17.4.1   Parsers for tokens
 * 
 * Writing constants is hard work, because we can either use the `OP_CONSTANT`
 * OR the `OP_CONSTANT_LONG`, depending on how many constants are in the current
 * chunk's constants pool.
 */
static inline void emit_constant(Compiler *self, TValue value) {
    int index = add_constant(current_chunk(self), value);
    if (index <= MAX_CONSTANTS_SHORT) {
        emit_bytes(self, OP_CONSTANT, (uint8_t)index);
    } else if (index <= MAX_CONSTANTS_LONG) {
        uint8_t hi  = (index >> 16) & 0xFF; // mask bits 17-24
        uint8_t mid = (index >> 8)  & 0xFF; // mask bits 9-16
        uint8_t lo  = index & 0xFF;         // mask bits 1-8
        emit_long(self, OP_CONSTANT_LONG, hi, mid, lo);
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
static inline void end_compiler(Compiler *self) {
    emit_return(self);
#ifdef DEBUG_PRINT_CODE
    if (!self->parser.haderror) {
        disassemble_chunk(current_chunk(self), "code");
    }
#endif
}

/**
 * BEGIN:       Forward Declarations
 */

static void compile_expression(Compiler *self);
static void parse_precedence(Compiler *self, Precedence precedence);

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
void binary(Compiler *self) {
    TokenType optype = self->parser.previous.type;
    const ParseRule *rule = get_rule(optype);
    // Compile right hand side, and evaluate it if it has higher precedence operations.
    // We use 1 higher precedence to ensure left-to-right associativity.
    parse_precedence(self, (Precedence)(rule->precedence + 1));

    switch (optype) {
    // -*- Equality and comparison operators ---------------------------------*-
    // For fun, let's try to use less cases. We do know the following: 
    // 1. a != b <=> !(a == b)
    // 2. a >= b <=> !(a < b)
    // 3. a <= b <=> !(a > b)
    case TOKEN_EQ: emit_byte(self, OP_EQ); break;
    case TOKEN_NEQ: emit_bytes(self, OP_EQ, OP_NOT); break;
    case TOKEN_GT: emit_byte(self, OP_GT); break;
    case TOKEN_GE: emit_bytes(self, OP_LT, OP_NOT);
    case TOKEN_LT: emit_byte(self, OP_LT); break;
    case TOKEN_LE: emit_bytes(self, OP_GT, OP_NOT); break;

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
 * III:18.4     Two New Types
 * 
 * Emits the literals `false`, `true` and `nil`.
 */
void literal(Compiler *self) {
    switch (self->parser.previous.type) {
    case TOKEN_FALSE: emit_byte(self, OP_FALSE); break;
    case TOKEN_NIL:   emit_byte(self, OP_NIL);   break;
    case TOKEN_TRUE:  emit_byte(self, OP_TRUE);  break;
    default: return; // Unreachable
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
void grouping(Compiler *self) {
    compile_expression(self);
    consume_token(self, TOKEN_RPAREN, "Expected ')' after grouping expression.");
}

/* Parse a number literal and emit it as a constant. */
void number(Compiler *self) {
    double value = strtod(self->parser.previous.start, NULL);
    emit_constant(self, makenumber(value));
}

/**
 * III:19.3     Strings
 * 
 * Here we go, strings! One of the big bads of C.
 */
void string(Compiler *self) {
    int start = self->parser.previous.start + 1;   // Past opening quote
    int length = self->parser.previous.length - 2; // Before closing quote
    lua_String *strobj = copy_string(start, length);
    emit_constant(self, makeobject(strobj));
}

/**
 * III:17.4.3   Unary negation
 * 
 * Assumes the leading '-' token has been consumed and is the parser's previous
 * token.
 */
void unary(Compiler *self) {
    // Keep in this stackframe's memory so that if we recurse, we evaluate the
    // topmost stack frame (innermosts, higher precedences) first and work our 
    // way down until we reach this particular function call.
    TokenType optype = self->parser.previous.type;
    
    // Compile the unary expression's operand, which may be a number literal,
    // another unary operator, a grouping, etc.
    parse_precedence(self, PREC_UNARY);

    // compile expression may recurse! When that's done, we emit this instruction.
    // Remember that opcodes look at the top of the stack for their operands, so
    // this is why we emit the opcode AFTER the compiled expression.
    switch (optype) {
    case TOKEN_NOT:   emit_byte(self, OP_NOT); break;
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
static void parse_precedence(Compiler *self, Precedence precedence) {
    advance_compiler(self);
    const ParseFn prefixfn = get_rule(self->parser.previous.type)->prefix;
    if (prefixfn == NULL) {
        error(self, "Expected an expression.");
        return; // Might end up with NULL infixfn as well
    }
    prefixfn(self);
    
    while (precedence <= get_rule(self->parser.current.type)->precedence) {
        advance_compiler(self);
        const ParseFn infixfn = get_rule(self->parser.previous.type)->infix;
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
static void compile_expression(Compiler *self) {
    parse_precedence(self, PREC_ASSIGNMENT); 
}

bool compile_bytecode(Compiler *self, const char *source) {
    init_lexer(&self->lexer, source); 
    advance_compiler(self);
    compile_expression(self);
    consume_token(self, TOKEN_EOF, "Expected end of expression.");
    end_compiler(self);
    return !self->parser.haderror;
}
