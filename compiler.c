#include "compiler.h"
#include "value.h"
#include "object.h"
#include "parserules.h"

static inline void init_locals(Locals *self) {
    self->count = 0;
    self->depth = 0;
}

void init_compiler(Compiler *self, lua_VM *lvm) {
    init_locals(&self->locals);
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

/* EMIT BYTECODE FUNCTIONS ---------------------------------------------- {{{ */

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
 * 
 * NOTE:
 * 
 * I've changed it to use a `DWord` so that we can cast between function pointers
 * for `emit_long`.
 */
static inline void emit_bytes(Compiler *self, Byte opcode, DWord operand) {
    emit_byte(self, opcode);
    emit_byte(self, operand);
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
static inline void emit_long(Compiler *self, Byte opcode, DWord operand) {
    Byte hi  = (operand >> 16) & 0xFF; // mask bits 17-24 : 0x010000..0xFFFFFF
    Byte mid = (operand >> 8)  & 0xFF; // mask bits 9-16  : 0x000100..0x00FFFF
    Byte lo  = operand & 0xFF;         // mask bits 1-8   : 0x000000..0x0000FF
    emit_byte(self, opcode);
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
 * created, we return 0. By itself this doesn't indicate an error, but because
 * we call the `error` function that sets the compiler's parser's error state.
 */
static inline DWord make_constant(Compiler *self, TValue value) {
    int constant = add_constant(current_chunk(self), value);
    if (constant > MAX_CONSTANTS_LONG) {
        parser_error(&self->parser, "Too many constants in the current chunk");       
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
    } else {
        parser_error(&self->parser, "Too many constants in current chunk.");
    }
}

/* }}} */

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
 * III:22.1     Block Statements
 * 
 * New scopes are denoted by simply incrementing `self->locals.depth`.
 * Remember that 0 indicates global scope, 1 indicates 1st top-level block scope.
 */
static void begin_scope(Compiler *self) {
    self->locals.depth++;
}

/**
 * III:22.1     Block Statements
 * 
 * The counterpart to `begin_scope`. In order to ensure correct compilation,
 * this must ALWAYS be called eventually after a call to `begin_scope()`.
 * 
 * Ending of current scope is done by simply decrementing `self->locals.depth`.
 * 
 * III:22.3     Declaring Local Variables
 * 
 * When a block ends we need to "free" the stack memory by decrementing the
 * number of locals we're counting so that the next push will overwrite the old
 * memory we used beforehand.
 */
static void end_scope(Compiler *self) {
    int poppable = 0;
    Locals *locals = &self->locals;
    locals->depth--;
    // Walk backward through the array looking for variables declared at the
    // scope depth we just left. Remember "freeing" is just decrementing here.
    while (locals->count > 0 && locals->stack[locals->count - 1].depth > locals->depth) {
        locals->count--;
        poppable++;
    }
    // Don't waste cycles on popping nothing.
    if (poppable > 0) {
        emit_bytes(self, OP_POPN, poppable);
    }
}

/* FORWARD DECLARATIONS ------------------------------------------------- {{{ */

static void expression(Compiler *self);
static void statement(Compiler *self);
static void declaration(Compiler *self);
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
 * III:22.3     Declaring Local Variables
 * 
 * Compare 2 Tokens on a length basis then a byte-by-byte basis.
 * 
 * NOTE:
 * 
 * Because Tokens aren't full lua_Strings, we have to do it the long way instead
 * of checking their hashes (which they have none).
 */
static bool identifiers_equal(const Token *lhs, const Token *rhs) {
    if (lhs->length != rhs->length) {
        return false;
    }
    return memcmp(lhs->start, rhs->start, lhs->length) == 0;
}

/**
 * III:22.4     Using Locals
 * 
 * Walk the list of locals currently in scope (backwards) looking for a token
 * that has the same identifier as the given name. We start with the last
 * declared variable so that inner local variables correctly shadow locals with
 * the same names in surrounding scopes.
 * 
 * We return the index of the found variable into the Locals stack array, else
 * we return -1 to indicate it's a global variable or undefined.
 * 
 * NOTE:
 * 
 * I've changed it so now we try to find the nearest variable in the nearest
 * outer scope that matches our identifier.
 * 
 * If it doesn't find a local variable in any surrounding scope of the same name,
 * it'll resort to looking up a global variable then. If that doesn't work you'll
 * likely get a runtime error.
 */
static DWord resolve_local(Compiler *self, const Token *name) {
    const Locals *locals = &self->locals;
    for (int i = locals->count - 1; i >= 0; i--) {
        const Local *var = &locals->stack[i];
        if (identifiers_equal(name, &var->name)) {
            // Implicitly continue if shadowing itself.
            if (var->depth != -1) {
                return i;
            } 
        }
    }
    // Indicate to caller they should try to lookup in the globals table.
    return MAX_DWORD;
}

/**
 * III:22.3     Declaring Local Variables
 * 
 * Initialize the next available slot in the Locals stack with the given token
 * and the Locals struct's current depth.
 * 
 * Lifetimes for things like strings are still ok because the entire source code 
 * string should be valid for the entirety of the compilation process.
 * 
 * III:22.4.2   Another scope edge case
 * 
 * What happens when we have this? `a=1; do local a=a; end;`
 * Or how about this? `do local a=1; do local a=a; end; end;`
 * 
 * This is where the concept of marking a local variable "uninitialized" and
 * "initialized" comes into play.
 * 
 * When marked uninitialized, the variable's depth is -1. This allows us to split
 * the declaration into 2 phases.
 */
static void add_local(Compiler *self, Token name) {
    Locals *locals = &self->locals;
    Parser *parser = &self->parser;
    if (locals->count >= LUA_MAXLOCALS) {
        parser_error(parser, "Too many local variables in function.");
        return;
    }
    Local *var = &locals->stack[locals->count++];
    var->name = name;
    var->depth = -1;
}

/**
 * III:22.3     Declaring Local Variables
 * 
 * Record the existing of a local variable (and local variables ONLY!).
 * Note that because global variables are late-bound, the compiler DOESN'T need
 * to keep track of which global declarations it's seen.
 * 
 * For locals however, we do need to keep track hence we add it to the list of
 * the compiler's local variables.
 */
static void declare_variable(Compiler *self, bool islocal) {
    Locals *locals = &self->locals;
    Parser *parser = &self->parser;
    // Bail out if this is called for global variable declarations.
    if (!islocal /* locals->depth == 0 */) {
        return;
    }
    const Token *name = &parser->previous;
    // Ensure identifiers are never shadowed/redeclared in the same scope.
    // Note that the current scope is at the END of the array.
    for (int i = locals->count - 1; i >= 0; i--) {
        const Local *var = &locals->stack[i];
        // If we hit an outer scope, stop looking for shadowed identifiers.
        if (var->depth != -1 && var->depth < locals->depth) {
            break;
        }
        if (identifiers_equal(name, &var->name)) {
            parser_error(parser, "Already a variable with this name in this scope.");
            return;
        }
    }
    add_local(self, *name);
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
 * 
 * III:21.4     Assignment
 * 
 * In order to meet the signature of `ParseFn`, we need to add a `bool` param.
 * It sucks but it's better to keep all the function pointers uniform!
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
    case TOKEN_CARET:  emit_byte(self, OP_POW); break;
    default: return;   // Should be unreachable, but clang insists on this.
    }
}

/**
 * III:18.4     Two New Types
 * 
 * Emits the literals `false`, `true` and `nil`.
 * 
 * III:21.4     Assignment
 * 
 * Adding a `bool` parameter for uniformity with all the other function pointers
 * in the `parserules.c` lookup table.
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
    expression(self);
    consume_token(&self->parser, TOKEN_RPAREN, "Expected ')' after grouping expression.");
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
    
    Parser *parser = &self->parser;
    const char *start = parser->previous.start + 1; // Past opening quote
    int length        = parser->previous.length - 2; // Length w/o quotes
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
 * 
 * III:22.4     Using Locals
 * 
 * NOTE:
 * 
 * One major difference is that because Lua doesn't allow nested declarations,
 * this function is only really used for variable retrieval, never assignment.
 * 
 * So in order to facilitate this, any call of `variable()` WITHIN an expression
 * will always set `assignable` to false.
 * 
 * Otherwise, when parsing a simple declaration (e.g. `TOKEN_IDENT` is the first
 * token in the statement) we assume it to be assignable.
 */
static void named_variable(Compiler *self, const Token *name, bool assignable) {
    Byte getop, setop;
    // This is absolutely horrendous but we need it in order to be able to
    // use one of `emit_bytes` or `emit_long`.
    void (*emitfn)(Compiler *self, Byte opcode, DWord operand) = emit_bytes;

    // Try to find local variable with given name. -1 indicates none found.
    DWord arg = resolve_local(self, name);
    if (arg != MAX_DWORD) {
        getop = OP_GETLOCAL;
        setop = OP_SETLOCAL; 
    } else {
        // Out of range error is handle by `make_constant()`.
        arg = identifier_constant(self, name);
        bool islong = (arg > MAX_CONSTANTS_SHORT && arg <= MAX_CONSTANTS_LONG);
        getop  = (islong) ? OP_GETGLOBAL_LONG : OP_GETGLOBAL;
        setop  = (islong) ? OP_SETGLOBAL_LONG : OP_SETGLOBAL;
        emitfn = (islong) ? emit_long : emit_bytes;
    }
    if (assignable && match_token(&self->parser, TOKEN_ASSIGN)) {
        expression(self);
        emitfn(self, setop, arg);
    } else {
        emitfn(self, getop, arg);
    }
}

/**
 * III:22.4     Using Locals
 * 
 * I've changed more of the semantics. Now, we can ONLY assign a variable
 * inside of simple assignment statements (e.g. `local x=1;` or `x=y;`).
 *
 * As in Lua we explicitly disallow nested assignments:
 * 
 * `local a=1; local b=2; local c=b=a`
 * 
 * And we also disallow assignment inside of other statements:
 * 
 * `local x=1; print(x = 2);`
 * 
 * NOTE:
 * 
 * This function is only ever called by `declaration()`, so that any other
 * usage of `named_variable()` is called by `variable()` (the prefixfn).
 * This allows us to specify when assignments are allowed.
 */
static inline void variable_assignment(Compiler *self) {
    // Don't consume the '=' just yet.
    if (!check_token(&self->parser, TOKEN_ASSIGN)) {
        parser_error(&self->parser, "Expected '=' after variable assignment.");       
        return;
    } 
    named_variable(self, &self->parser.previous, true);    
    match_token(&self->parser, TOKEN_SEMICOL);
}

/**
 * III:21.3     Reading Variables
 * 
 * We access a variable using its name.
 * 
 * Assumes the identifier token is the parser's previous one as we consumed it.
 * 
 * III:22.4     Using Locals
 * 
 * I've changed the semantics so that this function, which is only ever called
 * by `expression()`, will never allow us to assign a variable.
 * This is because simple variable assignment is detected by `declaration()`.
 */
void variable(Compiler *self) {
    named_variable(self, &self->parser.previous, false);
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
 * 
 * III:21.4     Assignment
 * 
 * One subtlety that arises from our current implementation is that:
 * `a * b = c + d` considers `a * b` as a valid assignment target! For most,
 * that won't make sense so we want to disallow it. 
 * 
 * But the only way to disallow such expressions from letting assignments 
 * through is to explicitly check if the current parsed precedence is greater 
 * than `PREC_ASSIGNMENT`.
 * 
 * In other words we need to append a `bool` argument to all parser functions!
 */
static void parse_precedence(Compiler *self, Precedence precedence) {
    Parser *parser = &self->parser;
    advance_parser(parser);
    const ParseFn prefixfn = get_rule(parser->previous.type)->prefix;
    if (prefixfn == NULL) {
        parser_error(parser, "Expected an expression.");
        return; // Might end up with NULL infixfn as well
    }
    bool assignable = (precedence <= PREC_ASSIGNMENT);
    prefixfn(self);
    
    while (precedence <= get_rule(parser->current.type)->precedence) {
        advance_parser(parser);
        const ParseFn infixfn = get_rule(parser->previous.type)->infix;
        infixfn(self);
    }
    
    // No function consumed the '=' token so we didn't properly assign.
    if (assignable && match_token(parser, TOKEN_ASSIGN)) {
        parser_error(parser, "Invalid assignment target.");
    }
}

/**
 * III:21.2     Variable Declarations
 * 
 * Because I'm implementing Lua we don't have a `var` keyword, so we have to be
 * more careful when it comes to determining if an identifier supposed to be a
 * global variable declaration/definition/assignment, or a local.
 * 
 * Assumes that we already consumed a TOKEN_IDENT and that it's now the parser's
 * previous token.
 */
static Byte parse_variable(Compiler *self, bool islocal) {
    declare_variable(self, islocal);
    // Locals aren't looked up by name at runtime so return a dummy index.
    if (islocal /* self->locals.depth > 0 */) {
        return 0;
    }
    return identifier_constant(self, &self->parser.previous);
}

/**
 * III:22.4.2   Another scope edge case
 * 
 * Once a local variable's initializer has been compiled, we mark it as such.
 */
static void mark_initialized(Compiler *self) {
    Locals *locals = &self->locals;
    locals->stack[locals->count - 1].depth = locals->depth;
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
 * 
 * III:22.4     Using Locals
 * 
 * Unlike Lox and C, which allow nested declarations/definitions/assignments,
 * Lua doesn't because there are no differences between global variable
 * declaration and assignment. So in Lua we don't allow these to nest, e.g.
 * `a = 1; a = a = 2;` is an invalid statment.
 */
static void define_variable(Compiler *self, DWord index, bool islocal) {
    // There is no code needed to create a local variable at runtime, since
    // all our locals live exclusively on the stack and not in a hashtable.
    if (islocal /* self->locals.depth > 0 */) {
        mark_initialized(self);
        return;
    }
    if (index <= MAX_CONSTANTS_SHORT) {
        emit_bytes(self, OP_SETGLOBAL, index);
    } else if (index <= MAX_CONSTANTS_LONG) {
        emit_long(self, OP_SETGLOBAL_LONG, index);
    } else {
        parser_error(&self->parser, "Too many global variable identifiers.");
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
static void expression(Compiler *self) {
    parse_precedence(self, PREC_ASSIGNMENT); 
}

/**
 * III:22.2     Block Statements
 * 
 * Assumes we already consumed the `do` token. Until we hit an `end` token, try
 * to compile everything in between. It could start with a variable declaration
 * hence we start with that, even if it's not a variable declaration the calls
 * to something like `print_statement()` will eventually be reached.
 */
static void block(Compiler *self) {
    Parser *parser = &self->parser;
    while (!check_token(parser, TOKEN_END) && !check_token(parser, TOKEN_EOF)) {
        declaration(self);
    }
    consume_token(parser, TOKEN_END, "Expected 'end' after block.");
    match_token(parser, TOKEN_SEMICOL); // Like most of Lua this is optional.
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
 * 
 * III:22.3     Declaring Local Variables
 * 
 * Local variables in Lua are denoted purely by the use of the `local` keyword.
 * Otherwise, without it you're just declaring global variables everywhere!
 * This is a common criticism of Lua (that I fully understand) but for consistency's
 * sake I choose to follow their design.
 * 
 * So, unlike Lox and C, we don't determine locality of a variable by how deep
 * down in the scope it is, but the use of the `local` keyword and said depth.
 * 
 * NOTE:
 * 
 * I've now changed it so that only local variables can be considered for
 * declaration statements. For globals, we implicitly shunt down to the 
 * `variable()` parse rule.
 */
static void variable_declaration(Compiler *self) {
    Parser *parser = &self->parser;
    consume_token(parser, TOKEN_IDENT, "Expected identifier after 'local'.");
    // Index of variable name (previous token) as appended into constants pool
    DWord index = parse_variable(self, true);
    if (match_token(parser, TOKEN_ASSIGN)) {
        expression(self);
    } else {
        emit_byte(self, OP_NIL); // Push nil to stack as a default value
    }
    match_token(parser, TOKEN_SEMICOL);
    define_variable(self, index, true);
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
static void expression_statement(Compiler *self) {
    expression(self);
    match_token(&self->parser, TOKEN_SEMICOL);
    emit_byte(self, OP_POP);
}

static void print_statement(Compiler *self) {
    expression(self);
    match_token(&self->parser, TOKEN_SEMICOL);
    emit_byte(self, OP_PRINT);
}

/**
 * III:21.1     Statements
 * 
 * "Declarations" are statements that bind names to values. Remember that in our
 * grammar, assignment is one of (if not the) lowest precedences. So we parse
 * it first above all else, normal non-name-binding statements will shunt over
 * to `statement()` and whatever that delegates to.
 * 
 * III:22.4     Using Locals
 * 
 * I've revamped the system a little bit so that the only "variable declaration"
 * statements are the ones starting with "local". By default, global variables
 * are created as needed and assigned which is taken care of by the call to
 * `expression_statement()` which eventually calls `variable()`.
 */
static void declaration(Compiler *self) {
    Parser *parser = &self->parser;
    if (match_token(parser, TOKEN_LOCAL)) {
        variable_declaration(self);
    } else if (match_token(parser, TOKEN_IDENT)) {
        variable_assignment(self);
    } else {
        statement(self);
    }
    if (parser->panicking) {
        synchronize_parser(parser);
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
 * 
 * III:22.2     Block Statements
 * 
 * In Lox (like in C), new blocks are denoted by balanced '{}'.
 * In Lua there are denoted by the `do` and `end` keywords.
 * Unlike in Lox and C, Lua REQUIRES these keywords in `for` and `while` loops.
 * 
 * III:22.4     Using Locals
 * 
 * This is now where global, local variable declarations will be sifted off to
 * and in turn defer responsibility down to `expression_statement()`.
 * 
 * Unfortunately this allows us to nest assignments which is not consistent
 * with Lua's official design.
 */
static void statement(Compiler *self) {
    Parser *parser = &self->parser;
    if (match_token(parser, TOKEN_PRINT)) {
        print_statement(self);
    } else if (match_token(parser, TOKEN_DO)) {
        begin_scope(self);
        block(self);
        end_scope(self);
    } else {
        expression_statement(self);
    }
    // Disallow lone/trailing semicolons that weren't consumed by statements.
    if (match_token(parser, TOKEN_SEMICOL)) {
        parser_error(parser, "Unexpected symbol.");
    }
}

bool compile_bytecode(Compiler *self, const char *source) {
    init_parser(&self->parser, source);
    begin_scope(self); // File-scope/REPL line scope is its own block scope.
    advance_parser(&self->parser);
    while (!match_token(&self->parser, TOKEN_EOF)) {
        declaration(self);
    }
    end_scope(self);
    end_compiler(self);
    return !self->parser.haderror;
}
