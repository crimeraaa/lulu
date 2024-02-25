#include "compiler.h"
#include "value.h"
#include "object.h"
#include "parserules.h"

static inline void init_parser(Parser *self) {
    self->haderror  = false;
    self->panicking = false;
}

void init_compiler(Compiler *self, lua_VM *lvm) {
    init_parser(&self->parser);
    self->vm = lvm;
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
static void error_at(Parser *self, const Token *token, const char *message) {
    if (self->panicking) {
        return; // Avoid cascading errors for user's sanity
    }
    self->haderror = true;
    self->panicking = true;
    fprintf(stderr, "[line %i] Error", token->line);
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing as the error token already has a message.
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
static inline void error(Parser *self, const char *message) {
    error_at(self, &self->previous, message);
}

/**
 * III:17.2.1   Handling syntax errors
 * 
 * If the lexer hands us an error token, we simply report its error message.
 * This is a wrapper around the more generic `error_at()` which can take in any
 * arbitrary error message.
 */
static inline void error_at_current(Parser *self, const char *message) {
    error_at(self, &self->current, message);
}

/**
 * III:17.2     Parsing Tokens
 * 
 * Assume the compiler should move to the next token. So the parser is set to
 * start a new token. This adjusts state of the compiler's parser and lexer.
 */
static void advance_parser(Parser *parser, Lexer *lexer) {
    parser->previous = parser->current;
    
    for (;;) {
        parser->current = tokenize(lexer);
        if (parser->current.type != TOKEN_ERROR) {
            break;
        }
        // Error tokens already point to an error message thanks to the lexer.
        error_at_current(parser, parser->current.start);
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
        advance_parser(&self->parser, &self->lexer);
        return;
    }
    error_at_current(&self->parser, message);
}

/**
 * III:21.1.1   Print Statements
 * 
 * Check if the parser's CURRENT (not PREVIOUS) token matches `expected`.
 */
static inline bool check_token(Parser *self, TokenType expected) {
    return self->current.type == expected;
}

/**
 * III:21.1.1   Print Statements
 * 
 * If token matches, consume it and return true. Otherwise return false. Nothing
 * more, nothing less. We don't throw the parser into a panic state.
 */
static bool match_token(Compiler *self, TokenType expected) {
    if (!check_token(&self->parser, expected)) {
        return false;
    }
    advance_parser(&self->parser, &self->lexer);
    return true;
}

/**
 * III:17.3     Emitting Bytecode
 * 
 * This function simply writes to the compiler's current chunk the given byte,
 * and we log line information based on the consumed token (parser's previous).
 */
static inline void emit_byte(Compiler *self, Byte byte) {
    write_chunk(current_chunk(self), byte, self->parser.previous.line);
}

/** 
 * Helper because we'll be using this a lot. Used mainly for an 8-bit instruction
 * that has an 8-bit operand, like `OP_CONSTANT`.
 */
static inline void emit_bytes(Compiler *self, Byte i, Byte ii) {
    emit_byte(self, i);
    emit_byte(self, ii);
}

/** 
 * Helper to emit a 1-byte instruction with a 24-bit operand, such as the
 * `OP_CONSTANT_LONG` and `OP_DEFINE_GLOBAL_LONG` instructions.
 * 
 * NOTE:
 * 
 * We actually just split the 24-bit operand into 3 8-bit ones so that each of 
 * them fits into the chunk's bytecode array. We'll need to decode them later in 
 * the VM using similar bitwise operations.
 */
static inline void emit_long(Compiler *self, Byte byte, DWord dword) {
    Byte hi  = (dword >> 16) & 0xFF; // mask bits 17-24 : 0x010000..0xFFFFFF
    Byte mid = (dword >> 8)  & 0xFF; // mask bits 9-16  : 0x000100..0x00FFFF
    Byte lo  = dword & 0xFF;         // mask bits 1-8   : 0x000000..0x0000FF
    emit_byte(self, byte);
    emit_byte(self, hi);
    emit_byte(self, mid);
    emit_byte(self, lo);
}

/* Helper because it's automatically called by `end_compiler()`. */
static inline void emit_return(Compiler *self) {
    emit_byte(self, OP_RET);
}

/**
 * III:21.2     Variable Declarations
 * 
 * Returns an index into the current chunk's constants array where `value` has
 * been appended to.
 * 
 * This function does NOT handle emitting the appropriate bytecode instructions
 * needed to load this constant at the determined index at runtime. For that,
 * please refer to `emit_constant()`.
 * 
 * NOTE:
 * 
 * If more than `MAX_CONSTANTS_LONG` (a.k.a. 2 ^ 24 - 1) constants have been
 * created, we return 0. Chunks reserve index 0 in their constants pool as a
 * sort of "black hole" where garbage/error values are thrown into.
 */
static inline DWord make_constant(Compiler *self, TValue value) {
    int constant = add_constant(current_chunk(self), value);
    if (constant > MAX_CONSTANTS_LONG) {
        error(&self->parser, "Too many constants in the current chunk");       
        return 0;
    }
    return constant;
}

/**
 * III:17.4.1   Parsers for tokens
 * 
 * Writing constants is hard work, because we can either use the `OP_CONSTANT`
 * OR the `OP_CONSTANT_LONG`, depending on how many constants are in the current
 * chunk's constants pool.
 */
static inline void emit_constant(Compiler *self, TValue value) {
    DWord index = make_constant(self, value);
    if (index <= MAX_CONSTANTS_SHORT) {
        emit_bytes(self, OP_CONSTANT, (Byte)index);
    } else if (index <= MAX_CONSTANTS_LONG) {
        emit_long(self, OP_CONSTANT_LONG, index);
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

/* FORWARD DECLARATIONS ------------------------------------------------- {{{ */

static void compile_expression(Compiler *self);
static void compile_statement(Compiler *self);
static void compile_declaration(Compiler *self);
static void parse_precedence(Compiler *self, Precedence precedence);

/* }}} */

/**
 * III:21.2     Variable Declaration
 * 
 * This function handles interning a variable name (as if it were a string) and
 * appending it to our chunk's constants array where we'll index into in order
 * to retrieve the variable name again at runtime.
 */
static DWord identifier_constant(Compiler *self, const Token *name) {
    lua_String *result = copy_string(self->vm, name->start, name->length);
    return make_constant(self, makeobject(LUA_TSTRING, result));
}

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
    case TOKEN_PERCENT: emit_byte(self, OP_MOD); break;
    default: return; // Should be unreachable.
    }
}

/**
 * Right-associative binary operators, mainly for exponentiation and concatenation.
 */
void rbinary(Compiler *self) {
    TokenType optype = self->parser.previous.type;
    const ParseRule *rule = get_rule(optype);
    // We use the same precedence so we can evaluate from right to left.
    parse_precedence(self, rule->precedence);
    
    switch (optype) {
    // -*- 19.4.1   Concatentation -------------------------------------------*-
    // Unlike Lox, Lua uses '..' for string concatenation rather than '+'.
    // I prefer a distinct operator anyway as '+' can be confusing.
    case TOKEN_CONCAT: emit_byte(self, OP_CONCAT); break;
    case TOKEN_CARET: emit_byte(self, OP_POW); break;
    default: return; // Should be unreachable, but clang insists on this.
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
    const char *start = self->parser.previous.start + 1; // Past opening quote
    int length = self->parser.previous.length - 2; // Length w/o quotes
    lua_String *object = copy_string(self->vm, start, length);
    emit_constant(self, makeobject(LUA_TSTRING, object));
}

/**
 * III:21.3     Reading Variables
 * 
 * Emit the bytes needed to access a variable from the chunk's constants pool.
 * 
 * III:21.4     Assignment
 * 
 * If we have a set expression (e.g. some token before a '=') we emit the bytes
 * needed to set the variable. This will be helpful later on when we add classes
 * and methods, e.g. `menu.brunch(sunday).beverage = "mimosa"` where `.beverage`
 * should NOT retrieve a value, but rather set one.
 * 
 * For now though we only worry about variables.
 * 
 * NOTE:
 * 
 * Because of how we handle global variable assignments and treat them the same
 * as global variable declarations, we can affort to omit the `if` branches for
 * compiling and emitting `OP_SET` or `OP_GET` instructions.
 * 
 * We assume (for now) that this function is only used for variable retrieval.
 */
static inline void named_variable(Compiler *self, const Token *name) {
    DWord arg = identifier_constant(self, name);
    if (arg <= MAX_CONSTANTS_SHORT) {
        emit_bytes(self, OP_GETGLOBAL, arg);
    } else if (arg <= MAX_CONSTANTS_LONG) {
        emit_long(self, OP_GETGLOBAL_LONG, arg);
    } else {
        error_at(&self->parser, name, "Unable to retrieve global variable.");
    }
}

/**
 * III:21.3     Reading Variables
 * 
 * We access a variable using its name.
 * 
 * Assumes the identifier token is the parser's previous one as we consumed it.
 */
void variable(Compiler *self) {
    named_variable(self, &self->parser.previous);
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
    advance_parser(&self->parser, &self->lexer);
    const ParseFn prefixfn = get_rule(self->parser.previous.type)->prefix;
    if (prefixfn == NULL) {
        error(&self->parser, "Expected an expression.");
        return; // Might end up with NULL infixfn as well
    }
    prefixfn(self);
    
    while (precedence <= get_rule(self->parser.current.type)->precedence) {
        advance_parser(&self->parser, &self->lexer);
        const ParseFn infixfn = get_rule(self->parser.previous.type)->infix;
        infixfn(self);
    }
}

/**
 * III:21.2     Variable Declarations
 * 
 * Because I'm implementing Lua we don't have a `var` keyword, so we have to be
 * more careful when it comes to determining if an identifier supposed to be a
 * global variable declaration/definition/assignment.
 * 
 * Assumes that we already consumed a TOKEN_IDENT and that it's now the parser's
 * previous token.
 */
static Byte parse_variable(Compiler *self, const char *message) {
    // consume_token(self, TOKEN_IDENT, message); // Do NOT use this for Lua!
    return identifier_constant(self, &self->parser.previous);
}

/**
 * III:21.2     Variable Declarations
 * 
 * Global variables are looked up by name at runtime. So the VM needs access to
 * the name obviously. A string can't fit in our bytecode stream so we instead
 * store the string in the constants table then index into it. That's why we
 * take an index. If said index is more than 8-bits, we emit a long instruction.
 * 
 * III:21.4     Assignment
 * 
 * Since Lua allows implicit declaration of global variables, we can afford to
 * drop the `OP_DEFINE_*` opcodes because for our purposes they function the
 * exact same as the `OP_SET*` opcodes.
 * 
 * For globals, this assumes that the result of the assignment expression has
 * already been pushed onto the top of the stack.
 */
static void define_variable(Compiler *self, DWord index) {
    if (index <= MAX_CONSTANTS_SHORT) {
        emit_bytes(self, OP_SETGLOBAL, index);
    } else if (index <= MAX_CONSTANTS_LONG) {
        emit_long(self, OP_SETGLOBAL_LONG, index);
    } else {
        error(&self->parser, "Too many global variable identifiers.");
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

/**
 * III:21.2     Variable Declarations
 * 
 * Unlike Lox, which has a dedicated `var` keyword, Lua has implicit variable
 * declarations. That is, no matter the scope, simply typing `a = ...` already
 * declares a global variable of the name "a" if it doesn't already exist.
 * 
 * NOTE:
 * 
 * Because I'm implementing Lua, my version DOESN'T use the "var" keyword. So we
 * already consumed the `TOKEN_IDENT` before getting here. How do we deal with
 * non-assignments of globals and global assignments?
 * 
 * For non-assignment of a global declaration, Lua considers that an error.
 * For assignment IN a global declaration, we'll have to be smart.
 */
static void compile_vardecl(Compiler *self) {
    // Index of variable name (previous token) as appended into constants pool
    DWord index = parse_variable(self, "Expected a variable name.");
    if (match_token(self, TOKEN_ASSIGN)) {
        compile_expression(self); // Push result of expression
    } else {
        // emit_byte(self, OP_NIL); // Push nil as default variable value
        error(&self->parser, "Expected '=' after variable declaration.");
    }
    match_token(self, TOKEN_SEMICOL);
    define_variable(self, index);
}

/**
 * III:21.1.2   Expression statements
 * 
 * In Lox, expression statements (exprstmt for short) are just expressions followed
 * by a ';'. In Lua, we allow up to 1 ';' only. Any more are considered errors.
 * 
 * Since it produces a side effect by pushing something onto the stack, such as
 * via the prefixfns, we have to "undo" that by emitting a pop instruction.
 */
static void compile_exprstmt(Compiler *self) {
    compile_expression(self);
    match_token(self, TOKEN_SEMICOL);
    emit_byte(self, OP_POP);
}

static void compile_printstmt(Compiler *self) {
    compile_expression(self);
    match_token(self, TOKEN_SEMICOL);
    emit_byte(self, OP_PRINT);
}

/**
 * III:21.1.3   Error synchronization
 * 
 * If we hit a compile error while parsing a previous statement we panic.
 * When we panic, we attempt to synchronize by moving the parser to the next
 * statement boundary. A statement boundary is a preceding token that can end
 * a statement, like a semicolon. Or a subsequent token that begins a statement,
 * like a control flow or declaration statement.
 */
static void synchronize_parser(Parser *parser, Lexer *lexer) {
    parser->panicking = false;
    
    while (parser->current.type != TOKEN_EOF) {
        if (parser->previous.type == TOKEN_SEMICOL) {
            // If more than 1 semicol, don't report the error once since we
            // reset the parser panic state.
            if (parser->current.type != TOKEN_SEMICOL) {
                return;
            }
        }
        switch (parser->current.type) {
        case TOKEN_FUNCTION:
        case TOKEN_LOCAL:
        case TOKEN_FOR:
        case TOKEN_IF:
        case TOKEN_WHILE:
        case TOKEN_PRINT:
        case TOKEN_RETURN: return;
        default:
            ; // Do nothing
        }
        // Consume token without doing anything with it
        advance_parser(parser, lexer);
    }
}

/**
 * III:21.1     Statements
 * 
 * "Declarations" are statements that bind names to values. Remember that in our
 * grammar, assignment is one of (if not the) lowest precedences. So we parse
 * it first above all else, normal non-name-binding statements will shunt over
 * to `compile_statement()` and whatever that delegates to.
 */
static void compile_declaration(Compiler *self) {
    if (match_token(self, TOKEN_IDENT)) {
        compile_vardecl(self);
    } else {
        compile_statement(self);
    }
    if (self->parser.panicking) {
        synchronize_parser(&self->parser, &self->lexer);
    }
}

/**
 * III:21.1     Statements
 * 
 * For now let's focus on getting the `print` statement (not function!) to work.
 * 
 * III:21.1.2   Expression statements
 * 
 * Now, if we don't see a `print` "keyword", we assume we must be looking at an
 * expression statement.
 */
static void compile_statement(Compiler *self) {
    if (match_token(self, TOKEN_PRINT)) {
        compile_printstmt(self);
    } else {
        compile_exprstmt(self);
    }
    // Disallow lone/trailing semicolons that weren't consumed by statements.
    if (match_token(self, TOKEN_SEMICOL)) {
        error(&self->parser, "Unexpected symbol.");
    }
}

bool compile_bytecode(Compiler *self, const char *source) {
    init_lexer(&self->lexer, source); 
    advance_parser(&self->parser, &self->lexer);
    while (!match_token(self, TOKEN_EOF)) {
        compile_declaration(self);
    }
    end_compiler(self);
    return !self->parser.haderror;
}
