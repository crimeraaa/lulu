#include <errno.h>
#include "compiler.h"
#include "value.h"
#include "object.h"
#include "parserules.h"
#include "table.h"
#include "vm.h"

void init_compiler(Compiler *self, Compiler *current, LVM *vm, FnType type) {
    self->enclosing = current;
    self->function  = new_function(vm); // self->vm not assigned yet!
    self->type = type;
    if (current != NULL) {
        // Potentially dangerous as I'm not certain if the jmp_buf is being
        // copied correctly.
        self->lex = current->lex;
    }
    // The first self->lex is assigned and initalized outside of this function.
    self->locals.count = 0;
    self->locals.depth = 0;
    self->vm = vm;

    // We can grab the name of the function from the previous token because we
    // already consumed the 'function' token, allocated an object and then
    // consumed the identifier.
    if (type != FNTYPE_SCRIPT) {
        const Token *name = &self->lex->consumed;
        TClosure *luafn  = &self->function->fn.lua;
        luafn->name = copy_string(self->vm, name->start, name->len);
    }

    // Compiler implicitly claims stack slot 0 for VM's internal use.
    Local *local = &self->locals.stack[self->locals.count++];
    local->depth = 0;
    local->name.start = "";
    local->name.len = 0;
}

/**
 * Report an error at the token that was just consumed.
 *
 * Assumes that `self->lex.errjmp` (of type `jmp_buf`) was properly set in
 * `compile_bytecode()`. Parser will report a message then do a longjmp.
 *
 * WARNING:
 *
 * Be VERY careful with using this as it'll do an unconditional jump, meaning
 * that heap allocations within functions may not be cleaned up properly and
 * things like that.
 */
static void compiler_error(Compiler *self, const char *message) {
    throw_lexerror(self->lex, message);
}

/**
 * Report and throw an error at the current token held by our LexState. This is
 * mainly helpful when we consume a token and check the next one.
 */
static void compiler_error_current(Compiler *self, const char *message) {
    throw_lexerror_current(self->lex, message);
}

/**
 * III:17.3     Emitting Bytecode
 *
 * For now, the current chunk is the one that got assigned to the compiler instance
 * when it was created in `interpret_vm()`. Later on this will get more complicated.
 *
 * III:24.2     Compiling to Function Objects
 *
 * From `&self->chunk` we changed it to `&self->function->chunk` so that we get
 * the current chunk being compiled no matter what. Neat that Bob managed to get
 * this down beforehand!
 */
static Chunk *current_chunk(Compiler *self) {
    return &self->function->fn.lua.chunk;
}

/* EMIT BYTECODE FUNCTIONS ---------------------------------------------- {{{ */

/**
 * III:17.3     Emitting Bytecode
 *
 * This function simply writes to the compiler's current chunk the given byte,
 * and we log line information based on the LexState's consumed token.
 */
static void emit_byte(Compiler *self, Byte byte) {
    write_chunk(current_chunk(self), byte, self->lex->lastline);
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
static void emit_bytes(Compiler *self, Byte opcode, DWord operand) {
    emit_byte(self, opcode);
    emit_byte(self, (Byte)operand);
}

/**
 * III:23.3     While Statements
 *
 * Because we need to jump backward, the main caller `while_statement()` should
 * have saved the instruction address of the loop's beginning.
 *
 * We use that to patch the jump such that we can jump backwards rather than
 *forwards.
 */
static void emit_loop(Compiler *self, size_t loopstart) {
    emit_byte(self, OP_LOOP);
    // +2 due to 2-byte jump operand.
    size_t offset = current_chunk(self)->count - loopstart + 2;
    if (offset >= LUA_MAXWORD) {
        compiler_error(self, "Loop body too large");
    }
    emit_byte(self, bytemask(offset, 1)); // bits 9-16
    emit_byte(self, bytemask(offset, 0)); // bits 1-8
}

/**
 * III:23.1     If Statements
 *
 * We emit a jump instruction along with 2 dummy bytes for its operand.
 *
 * Return the index of the jump opcode into the chunk's code array.
 * We'll use it later to backpatch the jump instruction with the actual
 * amount of bytes to jump forward or backward.
 */
static size_t emit_jump(Compiler *self, Byte instruction) {
    emit_byte(self, instruction);
    emit_byte(self, 0xFF);
    emit_byte(self, 0xFF);
    return current_chunk(self)->count - LUA_OPSIZE_BYTE2;
}

/**
 * Helper to emit a 1-byte instruction with a 24-bit operand, such as the
 * `OP_LCONSTANT` and `OP_DEFINE_GLOBAL_LONG` instructions.
 *
 * NOTE:
 *
 * We actually just split the 24-bit operand into 3 8-bit ones so that each of
 * them fits into the chunk's bytecode array. We'll need to decode them later in
 * the VM using similar bitwise operations.
 */
static void emit_long(Compiler *self, Byte opcode, DWord operand) {
    emit_byte(self, opcode);
    emit_byte(self, bytemask(operand, 2));
    emit_byte(self, bytemask(operand, 1));
    emit_byte(self, bytemask(operand, 0));
}

/**
 * Helper because it's automatically called by `end_compiler()`.
 *
 * III:24.5.4   Returning from functions
 *
 * Currently we simply return an implicit nil value so that the stack has at
 * least *something* to pop off.
 */
static void emit_return(Compiler *self) {
    emit_byte(self, OP_NIL);
    emit_byte(self, OP_RETURN);
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
 * If more than `LUA_MAXLCONSTANTS` (a.k.a. 2 ^ 24 - 1) constants have been
 * created, we return 0. By itself this doesn't indicate an error, but because
 * we call the `error` function that sets the compiler's lex's error state.
 */
static DWord make_constant(Compiler *self, const TValue *value) {
    size_t index = add_constant(current_chunk(self), value);
    if (index > LUA_MAXLCONSTANTS) {
        compiler_error(self, "Too many constants in the current chunk.");
    }
    return (DWord)index;
}

/**
 * III:17.4.1   Parsers for tokens
 *
 * Writing constants is hard work, because we can either use the `OP_CONSTANT`
 * OR the `OP_LCONSTANT`, depending on how many constants are in the current
 * chunk's constants pool.
 */
static void emit_constant(Compiler *self, const TValue *value) {
    DWord index = make_constant(self, value);
    if (index <= LUA_MAXCONSTANTS) {
        emit_bytes(self, OP_CONSTANT, index);
    } else if (index <= LUA_MAXLCONSTANTS) {
        emit_long(self, OP_LCONSTANT, index);
    } else {
        compiler_error(self, "Too many constants in current chunk");
    }
}

/**
 * III:23.1     If Statements
 *
 * Go back to the bytecode, looking for the jump opcode itself, and backpatch
 * its 2 operands correctly.
 */
static void patch_jump(Compiler *self, size_t opindex) {
    // Adjust for the bytecode of the jump offset itself and its operands.
    QWord offset = current_chunk(self)->count - opindex - LUA_OPSIZE_BYTE2;
    if (offset >= LUA_MAXWORD) {
        compiler_error(self, "Too much bytecode to jump over");
    }
    current_chunk(self)->code[opindex]     = bytemask(offset, 1);
    current_chunk(self)->code[opindex + 1] = bytemask(offset, 0);
}

/* }}} */

/**
 * III:17.3     Emitting Bytecode
 *
 * For now we always emit a return for the compiler's current chunk.
 * This makes it so we don't have to remember to do it as ALL chunks need it.
 *
 * We now also return the function of this particular compiler instance, so that
 * nested function definitions can be emitted properly into the main compiler
 * instance.
 */
static Proto *end_compiler(Compiler *self) {
    emit_return(self);
    Proto *tagfn = self->function;
    TClosure *luafn = &tagfn->fn.lua;
#ifdef DEBUG_PRINT_CODE
    if (!self->lex->haderror) {
        // Implicit main function does not have a name.
        const char *name = (luafn->name) ? luafn->name->data : "(script)";
        disassemble_chunk(current_chunk(self), name);
    }
#endif
    return tagfn;
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

static int pop_scope(Compiler *self) {
    const Locals *locals = &self->locals;
    int poppable = 0;
    int count = locals->count;
    // Walk backward through the array looking for variables declared at the
    // scope depth we just left. Remember "freeing" is just decrementing here.
    while (count > 0 && locals->stack[count - 1].depth > locals->depth) {
        count--;
        poppable++;
    }
    // Don't waste cycles on popping nothing.
    if (poppable > 0) {
        emit_bytes(self, OP_NPOP, poppable);
    }
    return poppable;
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
    self->locals.depth--;
    self->locals.count -= pop_scope(self);
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
    TString *s = copy_string(self->vm, name->start, name->len);
    TValue o   = makestring(s);
    return make_constant(self, &o);
}

/**
 * III:22.3     Declaring Local Variables
 *
 * Compare 2 Tokens on a len basis then a byte-by-byte basis.
 *
 * NOTE:
 *
 * Because Tokens aren't full lua_Strings, we have to do it the long way instead
 * of checking their hashes (which they have none).
 */
static bool identifiers_equal(const Token *lhs, const Token *rhs) {
    if (lhs->len != rhs->len) {
        return false;
    }
    return memcmp(lhs->start, rhs->start, lhs->len) == 0;
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
    return LUA_MAXDWORD;
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
    if (locals->count >= LUA_MAXLOCALS) {
        compiler_error(self, "Too many local variables in function body");
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
    // Bail out if this is called for global variable declarations.
    if (!islocal) {
        return;
    }
    Locals *locals = &self->locals;
    const Token *name = &self->lex->consumed;
    // Ensure identifiers are never shadowed/redeclared in the same scope.
    // Note that the current scope is at the END of the array.
    for (int i = locals->count - 1; i >= 0; i--) {
        const Local *var = &locals->stack[i];
        // If we hit an outer scope, stop looking for shadowed identifiers.
        if (var->depth != -1 && var->depth < locals->depth) {
            break;
        }
        if (identifiers_equal(name, &var->name)) {
            compiler_error(self, "Redeclaration of local variable in same scope");
        }
    }
    add_local(self, *name);
}

/**
 * III:21.2     Variable Declarations
 *
 * Because I'm implementing Lua we don't have a `var` keyword, so we have to be
 * more careful when it comes to determining if an identifier supposed to be a
 * global variable declaration/definition/assignment, or a local.
 *
 * Assumes that we already consumed a TK_IDENT and that it's now the lex's
 * previous token.
 */
static DWord parse_variable(Compiler *self, const char *message, bool islocal) {
    LexState *lex = self->lex;
    consume_token(lex, TK_IDENT, message);
    declare_variable(self, islocal);
    // Locals aren't looked up by name at runtime so return a dummy index.
    if (islocal) {
        return 0;
    }
    return identifier_constant(self, &lex->consumed);
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
 *
 * III:24.7     Native Functions
 *
 * With my refactoring of the API, `OP_SETGLOBAL` just calls `lua_setglobal`
 * which is a macro for `lua_setfield`, which pops the expression for you.
 */
static void define_variable(Compiler *self, DWord index, bool islocal) {
    // There is no code needed to create a local variable at runtime, since
    // all our locals live exclusively on the stack and not in a hashtable.
    if (islocal) {
        mark_initialized(self);
        return;
    }
    if (index <= LUA_MAXCONSTANTS) {
        emit_bytes(self, OP_SETGLOBAL, index);
    } else if (index <= LUA_MAXLCONSTANTS) {
        emit_long(self, OP_LSETGLOBAL, index);
    } else {
        compiler_error(self, "Too many global variable identifiers.");
    }
}

/**
 * III:24.5     Function Calls
 *
 * Count the number of arguments in the given argument list that we compiled.
 */
static Byte arglist(Compiler *self) {
    int argc = 0;
    LexState *lex = self->lex;
    if (!check_token(lex, TK_RPAREN)) {
        do {
            // Push expression needed to resolve argument. We specifically need
            // to call it like this to ensure assignments don't occur within.
            expression(self);
            if (argc >= LUA_MAXBYTE) {
                compiler_error(self, "Cannot have more than 255 arguments");
            }
            argc++;
        } while (match_token(lex, TK_COMMA));
    }
    consume_token(lex, TK_RPAREN, "Expected ')' after argument list");
    return (Byte)argc;
}

/**
 * III:23.2     Logical Operators
 *
 * Logical and does what it can to resolve to a falsy value.
 *
 * Assumes the left hand expression has already been compiled and that its value
 * is currently at the top of the stack.
 *
 * If the value is falsy we immediately break out of the expression and leave the
 * result of the left hand expression on top of the stack for caller's use.
 *
 * Otherwise, we try to evaluate the right hand expression. We pop off the left
 * hand expression as it won't be used anymore. Whatever the right hand's result
 * is, it'll be the top of the stack that the caller ends up using.
 *
 * VISUALIZATION:
 *
 *      left operand expression
 * +--- OP_FJMP
 * |    OP_POP
 * |    right operand expression
 * +--> continue...
 */
void and_(Compiler *self) {
    size_t endjump = emit_jump(self, OP_FJMP);
    emit_byte(self, OP_POP);
    parse_precedence(self, PREC_AND);
    patch_jump(self, endjump);
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
 * associate various lex functions with each token type.
 *
 * e.g. '-' is `TK_DASH` which is associated with the prefix lex function
 * `unary()` and the infix lex function `binary()`.
 *
 * III:21.4     Assignment
 *
 * In order to meet the signature of `ParseFn`, we need to add a `bool` param.
 * It sucks but it's better to keep all the function pointers uniform!
 */
void binary(Compiler *self) {
    TkType optype = self->lex->consumed.type;
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
    case TK_EQ:      emit_byte(self,  OP_EQ);         break;
    case TK_NEQ:     emit_bytes(self, OP_EQ, OP_NOT); break;
    case TK_GT:      emit_byte(self,  OP_GT);         break;
    case TK_GE:      emit_bytes(self, OP_LT, OP_NOT); break;
    case TK_LT:      emit_byte(self,  OP_LT);         break;
    case TK_LE:      emit_bytes(self, OP_GT, OP_NOT); break;

    case TK_PLUS:    emit_byte(self, OP_ADD); break;
    case TK_DASH:    emit_byte(self, OP_SUB); break;
    case TK_STAR:    emit_byte(self, OP_MUL); break;
    case TK_SLASH:   emit_byte(self, OP_DIV); break;
    case TK_PERCENT: emit_byte(self, OP_MOD); break;
    default: return; // Should be unreachable.
    }
}

/**
 * III:24.5     Function Calls
 *
 * Assumes we consumed the '(' token, emits `OP_CALL` along with 1-byte operand
 * representing how many arguments are needed.
 *
 * NOTE:
 *
 * I think it's better to emit the number of *expressions* to evaluate for the
 * function call in order to determine where the base pointer should lie? But
 * this approach will definitely break if a function returns multiple values and
 * the results of which are used in another function call...
 */
void call(Compiler *self) {
    Byte argc = arglist(self);
    emit_bytes(self, OP_CALL, argc);
}

/**
 * Right-associative binary operators, mainly for exponentiation and concatenation.
 */
void rbinary(Compiler *self) {
    TkType optype = self->lex->consumed.type;
    const ParseRule *rule = get_rule(optype);
    // We use the same precedence so we can evaluate from right to left.
    parse_precedence(self, rule->precedence);
    switch (optype) {
    // -*- 19.4.1   Concatentation -------------------------------------------*-
    // Unlike Lox, Lua uses '..' for string concatenation rather than '+'.
    // I prefer a distinct operator anyway as '+' can be confusing.
    case TK_CONCAT: emit_byte(self, OP_CONCAT); break;
    case TK_CARET:  emit_byte(self, OP_POW); break;
    default: return; // Should be unreachable, but clang insists on this.
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
    switch (self->lex->consumed.type) {
    case TK_FALSE: emit_byte(self, OP_FALSE); break;
    case TK_NIL:   emit_byte(self, OP_NIL);   break;
    case TK_TRUE:  emit_byte(self, OP_TRUE);  break;
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
    consume_token(self->lex, TK_RPAREN, "Expected ')' after grouping expression");
}

/* Parse a number literal and emit it as a constant. */
void number(Compiler *self) {
    char *last;
    const Token *tk = &self->lex->consumed;
    const char *end = tk->start + tk->len;
    lua_Number n    = lua_str2num(tk->start, &last); // May be nan or inf

    // Can't use `check_tonumber` since it's very likely we don't end on a nul.
    if (last != end) {
        compiler_error(self, "Malformed number");
    } else {
        TValue v = makenumber(n);
        emit_constant(self, &v);
    }
}

/**
 * III:23.2.1   Logical or operator
 *
 * This one's a bit tricky because there's a bit more going on. Logical or, in a
 * sense, does what it can to resolve to truthy value.
 *
 * If the left side is truthy we skip over the right operand and leave the truthy
 * value on top of the stack for the caller to use.
 *
 * Otherwise, we pop the value, evaluate the right side and leave that resulting
 * value on top of the stack.
 *
 * VISUALIZATION:
 *
 *          left operand expression
 *     +--- OP_FJMP
 * +---|--- OP_JMP
 * |   +--> OP_POP
 * |        right operand expression
 * +------> continue...
 */
void or_(Compiler *self) {
    size_t elsejump = emit_jump(self, OP_FJMP);
    size_t endjump  = emit_jump(self, OP_JMP);
    patch_jump(self, elsejump);
    // Pop expression left over from condition to clean up stack.
    emit_byte(self, OP_POP);
    parse_precedence(self, PREC_OR);
    patch_jump(self, endjump);
}

/**
 * III:19.3     Strings
 *
 * Here we go, strings! One of the big bads of C.
 */
void string(Compiler *self) {
    const Token *token = &self->lex->consumed;
    // Point past first quote, use length w/o both quotes.
    TString *s = copy_string(self->vm, token->start + 1, token->len - 2);
    TValue o   = makestring(s);
    emit_constant(self, &o);
}

/**
 * Assumes we consumed the '{' token.
 */
void table(Compiler *self) {
    LexState *lex = self->lex;
    Table *t = new_table(self->vm);
    TValue o = maketable(t);
    if (!check_token(lex, TK_RCURLY)) {
        compiler_error_current(self, "Table fields not yet supported");
    }
    emit_constant(self, &o);
    consume_token(lex, TK_RCURLY, "Expected '}' after table declaration");
}

typedef void (*EmitFn)(Compiler *self, Byte opcode, DWord operand);

typedef struct {
    EmitFn emitfn;
    DWord index;
    Byte getop;    // One of `OP_GET` opcodes.
    Byte setop;    // One of the `OP_SET` opcodes.
} VarInfo;

/**
 * Helper function to look up the given Token `name` by checking if it's already
 * in the locals array. If it's not, then we assume it's a global variable and
 * store the instructions in a struct called `VarInfo`.
 */
static VarInfo resolve_variable(Compiler *self, const Token *name) {
    VarInfo expr;
    DWord arg = resolve_local(self, name);
    if (arg != LUA_MAXDWORD) {
        expr.index = arg;
        expr.getop = OP_GETLOCAL;
        expr.setop = OP_SETLOCAL;
        expr.emitfn = emit_bytes;
    } else {
        // Out of range error is handle by `make_constant()`.
        arg = identifier_constant(self, name);
        bool islong = (arg > LUA_MAXCONSTANTS && arg <= LUA_MAXLCONSTANTS);
        expr.index  = arg;
        expr.getop  = (islong) ? OP_LGETGLOBAL : OP_GETGLOBAL;
        expr.setop  = (islong) ? OP_LSETGLOBAL : OP_SETGLOBAL;
        expr.emitfn = (islong) ? emit_long     : emit_bytes;
    }
    return expr;
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
 * Otherwise, when parsing the first token in the statement we assume it to be
 * assignable.
 */
static void named_variable(Compiler *self, bool assignable) {
    LexState *lex = self->lex;
    const VarInfo vi = resolve_variable(self, &lex->consumed);

    // assignable is only true when this is called by `variable_statement()`.
    // You cannot have get expressions as lone statements e.g:
    //
    // PI=3.14
    // PI
    //
    // The 2nd 'PI' is invalid because it does nothing but would otherwise emit
    // a get expression that pushes to the stack but never pops it. Over time
    // this will overflow the stack!
    if (assignable) {
        if (match_token(lex, TK_ASSIGN)) {
            expression(self);
            vi.emitfn(self, vi.setop, vi.index);
        } else if (check_token(lex, TK_LPAREN)) {
            vi.emitfn(self, vi.getop, vi.index);
        } else {
            compiler_error(self, "'=' or '(' expected");
        }
    } else {
        // Otherwise we simply emit instructions to get variable's value. If it
        // is is a function, one of `variable_statement()`/`parse_precedence()`
        // will detect the '(' token.
        vi.emitfn(self, vi.getop, vi.index);
    }
}

/**
 * III:21.3     Reading Variables
 *
 * We access a variable using its name.
 *
 * Assumes the identifier token is the lex's previous one as we consumed it.
 *
 * III:22.4     Using Locals
 *
 * I've changed the semantics so that this function, which is only ever called
 * by `expression()` and `parse_precedence()`, will never allow us to assign a
 * variable. Simple variable assignment is detected by `statement()` which also
 * takes care of resolving function calls and the like.
 */
void variable(Compiler *self) {
    named_variable(self, false);
}

/**
 * III:17.4.3   Unary negation
 *
 * Assumes the leading '-' token has been consumed and is the lex's previous
 * token.
 */
void unary(Compiler *self) {
    // Keep in this stackframe's memory so that if we recurse, we evaluate the
    // topmost stack frame (innermosts, higher precedences) first and work our
    // way down until we reach this particular function call.
    TkType optype = self->lex->consumed.type;

    // Compile the unary expression's operand, which may be a number literal,
    // another unary operator, a grouping, etc.
    parse_precedence(self, PREC_UNARY);

    // compile expression may recurse! When that's done, we emit this instruction.
    // Remember that opcodes look at the top of the stack for their operands, so
    // this is why we emit the opcode AFTER the compiled expression.
    switch (optype) {
    case TK_NOT:   emit_byte(self, OP_NOT); break;
    case TK_DASH:  emit_byte(self, OP_UNM); break;
    default: return; // Should be unreachable.
    }
}

/**
 * III:17.6.1   Parsing with precedence
 *
 * By definition, all first tokens (literals, parentheses, variable names) are
 * considered "prefix" expressions. This helps kick off the compiler + lex.
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
 * In other words we need to append a `bool` argument to all lex functions!
 */
static void parse_precedence(Compiler *self, Precedence precedence) {
    LexState *lex = self->lex;
    next_token(lex);
    const ParseFn prefixfn = get_rule(lex->consumed.type)->prefix;
    if (prefixfn == NULL) {
        compiler_error(self, "Expected an expression");
    }
    prefixfn(self);

    while (precedence <= get_rule(lex->token.type)->precedence) {
        next_token(lex);
        const ParseFn infixfn = get_rule(lex->consumed.type)->infix;
        infixfn(self);
    }

    // No function consumed the '=' token so we didn't properly assign.
    if (match_token(lex, TK_ASSIGN)) {
        compiler_error_current(self, "Invalid assignment target");
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
 *
 * NOTE:
 *
 * I've now made it so assignment is NOT an expression. This allows us to make
 * the assumption that occurences of '=' outside of statements are errors.
 */
static void expression(Compiler *self) {
    parse_precedence(self, (Precedence)(PREC_ASSIGNMENT+1));
}

/**
 * III:22.2     Block Statements
 *
 * Assumes we already consumed the `do` token. Until we hit an `end` token, try
 * to compile everything in between. It could start with a variable declaration
 * hence we start with that, even if it's not a variable declaration the calls
 * to something like `print_statement()` will eventually be reached.
 *
 * III:22.3     If Statements
 *
 * I've changed the name to reflect that although the semantics of blocks in
 * 'do-end' and 'if-then-[else]-end' statements are similar, the exact keywords
 * we look for are somewhat different. This function will NOT work correctly
 * in an if-statement so we need a dedicated function for that.
 *
 * III:23.3     While Statements
 *
 * I've updated this so that it automatically creates a new block for itself.
 */
static void doblock(Compiler *self) {
    LexState *lex = self->lex;
    begin_scope(self);
    while (!check_token(lex, TK_END, TK_EOF)) {
        declaration(self);
    }
    end_scope(self);
    consume_token(lex, TK_END, "Expected 'end' after 'do' block");
    match_token(lex, TK_SEMICOL); // Like most of Lua this is optional.
}

/**
 * III:24.4:    Function Declarations
 *
 * Create a new compiler instance to house a new function's bytecode, and then
 * emit that function into the caller compiler's constants chunk.
 *
 * This is different from the other `emit_*` functions due to how complicated it
 * is and how much it relies on other static functions.
 */
static void emit_function(Compiler *self, FnType type) {
    Compiler _next = {0};
    init_compiler(&_next, self, self->vm, type);

    // To handle compiling multiple functions nested within each other, we use
    // local compiler instances per stack frame of `function()` (C, not Lua!)
    Compiler *next = &_next;

    // Set this AFTER initializing to ensure we're pointing at the one that was
    // copied over from `self`.
    LexState *lex = next->lex;

    // Poke at the address of the Lua function part so we can populate its
    // members, mostly the arity.
    TClosure *luafn = &next->function->fn.lua;

    // Local scope to capture our arguments. Note that we don't have a paired
    // call to `end()` because we effectively throw out the local compiler
    // instance anyway near the end.
    begin_scope(next);
    consume_token(lex, TK_LPAREN, "Expected '(' after function declaration");
    if (!check_token(lex, TK_RPAREN)) {
        // Since compiler instances have their own locals stack, we have to
        // take care to pass the CORRECT pointer!
        do {
            luafn->arity++;
            // Possibly risky as I'm not sure if the 'errjmp' member was copied
            // correctly.
            if (luafn->arity > LUA_MAXBYTE) {
                compiler_error(self, "More than 255 parameters");
            }
            // Semantically parameters are just local variables declared in the
            // outermost lexical scope of the function body.
            Byte index = parse_variable(next, "Expected parameter name", true);
            define_variable(next, index, true);
        } while (match_token(lex, TK_COMMA));
    }
    consume_token(lex, TK_RPAREN, "Expected ')' after parameters");
    doblock(next);

    Proto *f = end_compiler(next);
    TValue o     = makefunction(f);
    emit_bytes(self, OP_CONSTANT, make_constant(self, &o));
}

/**
 * III:24.7     Native Functions
 *
 * Assumes we consumed the 'function' keyword, and that we only have a '(' next.
 *
 * This isn't the same as the one in the book, this is my revamp of the API so
 * we can assign anonymous functions to variables e.g. `i = function() ... end`.
 */
void function(Compiler *self) {
    if (match_token(self->lex, TK_IDENT)) {
        compiler_error(self, "Cannot bind name to anonymous function here");
    }
    emit_function(self, FNTYPE_FUNCTION);
}

/**
 * III:24.4     Function Declarations
 *
 * For now we'll only work with global functions, local functions are too much
 * to think about. This function assumes we consumed the 'function' keyword, and
 * that an identifier is sitting waiting to be consumed.
 *
 * NOTE:
 *
 * I've now added support for local functions. It wasn't that difficult it turns
 * out! Since functions are first-class values, all we needed to do was to emit
 * them to the constans chunk, then push them to the stack and pop as needed.
 */
static void function_declaration(Compiler *self, bool islocal) {
    DWord index = parse_variable(self, "Expected identifier after 'function'", islocal);

    // Emit this already because function params will append stuff to the locals
    // array, so calling mark_initalized() then won't work how we want it to.
    if (islocal) {
        mark_initialized(self);
    }
    emit_function(self, FNTYPE_FUNCTION);
    define_variable(self, index, islocal);
}

static void define_locals(Compiler *self, int count) {
    Locals *locals = &self->locals;
    // Can't use `define_variable` as it only uses count - 1, so we have to
    // manually mark this as initialized.
    // We iterate backwards as the first local is farther down the stack.
    for (int i = count - 1; i >= 0; i--) {
        locals->stack[locals->count - i - 1].depth = locals->depth;
    }
}

/**
 * Returns the negative offset of the first local variable pushed to the stack.
 * That is, the absolute index is `locals[count - 1 - offset]`.
 */
static int declare_locals(Compiler *self) {
    LexState *lex = self->lex;
    int count = 1; // Always assume we have at least 1 identifier.
    // Resolve local variable identifiers
    do {
        parse_variable(self, "Expected identifier", true);
        count++;
        // Move here so current token is properly reported.
        if (count > LUA_MAXMULTIVAL) {
            compiler_error(self, "Too many declarations in comma-separated list");
        }
    } while (match_token(lex, TK_COMMA));
    return count - 1; // For our sanity in indexing, make it 0-based.
}

static void assign_locals(Compiler *self, int count) {
    int exprs = 0; // 0-based but assumes we have at least 1 right hand expr.
    do {
        expression(self);
        exprs++;
    } while (match_token(self->lex, TK_COMMA));

    if (exprs == count) {
        return;
    }
    // Too many expressions, not enough declarations.
    if (exprs > count) {
        emit_bytes(self, OP_NPOP, exprs - count);
        return;
    }
    // Too few expressions for given number of declarations.
    for (int i = 0, j = count - exprs; i < j; i++) {
        emit_byte(self, OP_NIL);
    }
}

/** III:21.2     Variable Declarations
 * Unlike Lox, which has a dedicated `var` keyword, Lua has implicit variable
 * declarations. That is, no matter the scope, simply typing `a = ...` already
 * declares a global variable of the name "a" if it doesn't already exist.
 *
 * NOTE:
 *
 * Because I'm implementing Lua, my version DOESN'T use the "var" keyword. So we
 * already consumed the `TK_IDENT` before getting here. How do we deal with
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
    LexState *lex = self->lex;
    int offset = declare_locals(self);
    if (match_token(lex, TK_ASSIGN)) {
        assign_locals(self, offset);
    } else {
        for (int i = 0; i < offset; i++) {
            emit_byte(self, OP_NIL);
        }
    }
    match_token(lex, TK_SEMICOL);
    define_locals(self, offset);
}

/**
 * Assumes we consumed some identifier token as the first expression in a
 * statement. For our purposes, we want to only ever allow a variable assignment
 * or a get expressions for a function call. A statement consisting of JUST a
 * single get expression is not allowed.
 */
static void variable_statement(Compiler *self) {
    LexState *lex = self->lex;
    named_variable(self, true);

    // Since functions are first-class values, we can detect function calls if
    // after emitting a value we have a '(' token. Also, since this is the first
    // expression of the statement, we can safely discard the return value.
    if (match_token(lex, TK_LPAREN)) {
        call(self);
        emit_byte(self, OP_POP);
    }
    match_token(lex, TK_SEMICOL);
}

/**
 * III:23.4:    For Statements
 *
 * Assumes we just consumed a 'for' token and we're sitting on the initializer.
 * e.g. in 'for i = 0, ...' we're concerned with the 'i = 0,' part.
 *
 * We return a `Token` of the variable identifier. Since it's a local variable
 * we need not worry about its index into the constants array. However we do NOT
 * yet define and mark it as initialized, in the condition segment we need to be
 * able to resolve outer instances of the identifier in the expression. e.g:
 *
 * `local i=2; for i=0, i+1 do ... end` we want the 'i' in the condition to
 * resolve to the outer local 'i=2', not the loop iterator 'i=0'.
 */
static Token for_initializer(Compiler *self) {
    LexState *lex = self->lex;
    const Token name = lex->token;

    // Iterator variable is always a local declaration.
    parse_variable(self, "Expected identifier", true);
    consume_token(lex, TK_ASSIGN, "Expected '=' after identifier");
    expression(self);
    consume_token(lex, TK_COMMA, "Expected ',' after 'for' initializer");
    return name;
}

/**
 * III:23.4     For Statements
 *
 * Given `token`, push a local variable identifier to the locals array without
 * much double checking. This identifier is not intended to be used from the
 * programmer's point of view, it's just here to ensure our loop state locals
 * are valid.
 */
static void push_unnamed_local(Compiler *self) {
    static const Token unnamed = {
        .type  = TK_IDENT,
        .start = NULL,
        .len   = 0,
    };
    add_local(self, unnamed);
    mark_initialized(self);
}

/**
 * III:23.4     For Statements
 *
 * In `for i=0, 4 do ... end' we're now concerned with the '4' part which is the
 * inclusive. That is, equivalent C code would be `for (int i=0; i<=4; i++)`.
 *
 * So we're concerned with implementing the `i<=4` part in Lua. However, Lua's
 * numeric for condition is a bit unique in that the local iterator variable is
 * implicit.
 *
 * Thus, in `for i=0, 4`, the '4' part implicitly compiles to 'i<=4'.
 *
 * EDGECASE:
 *
 * `for i=0, i do ... end` should throw a runtime error since 'i' in the condition
 * is not an external local or a global. This is why we delay defining and
 * marking the iterator as initialized because we want to attempt to resolve
 * token instances of the same identifier in the condition expression.
 *
 * Only after compiling the condition expression do we actually define and mark
 * the iterator as initialized. We get the correct index into the locals array
 * using `resolve_local()` which is why we need the Token `name`.
 *
 * TODO:
 *
 * Currently, our implementation constantly evaluates the condition expression.
 * That is, `local x=2; for i=0,x+1 do ... end` constantly computes `x+1`.
 * In Lua it's actually only evaluated once, any mutations to `x` won't affect
 * the condition.
 */
static DWord for_limit(Compiler *self, const Token *name) {
    // Emit the expression first so we can attempt to resolve outer instances of
    // our iterator variable, THEN we define and resolve the iterator.
    // (iter <= cond) <=> (cond > iter)
    expression(self);

    // We haven't declared any other locals so we can safely assume the topmost
    // one is the iterator which we now need to mark as initialized.
    mark_initialized(self);

    // Now that it's been initialized we can get the correct index into the
    // locals array.
    DWord index = resolve_local(self, name);

    // We emit an unnamed local variable for the condition so that it's evaluated
    // exactly once, then we can just access it from the stack as needed.
    push_unnamed_local(self);
    return index;
}

static size_t for_increment(Compiler *self) {
    // 'for' increment is a bit convoluted.
    if (match_token(self->lex, TK_COMMA)) {
        expression(self);
    } else {
        // Positive increment of 1 is our default.
        static const TValue incr = makenumber(1);
        emit_constant(self, &incr);
    }
    push_unnamed_local(self);
    emit_byte(self, OP_FORPREP); // Signal to VM to check the arguments.
    emit_byte(self, 0xff);       // Dummy offset.
    return current_chunk(self)->count - LUA_OPSIZE_BYTE;
}

/**
 * III:24.7     Native Functions
 *
 * Now it's up to the VM to use these 3 values as it needs. We need to first
 * evaluate if the increment is negative or not. It's up to the VM to emit
 * a `true` or `false` at runtime to determine if we should jump.
 */
static size_t emit_for_limit(Compiler *self, DWord iter, size_t prep) {
    // Default: iterator <= condition is the same as !(iterator > condition)
    // However, in FORPREP the VM will check if the increment is negative.
    // If it is, we modify the code at runtime to replace OP_GT with OP_LT.
    emit_bytes(self, OP_GETLOCAL, iter);
    emit_bytes(self, OP_GETLOCAL, iter + 1);
    emit_bytes(self, OP_GT, OP_NOT);
    
    // Woohoo self modifying code! Since the above instructions are always
    // emitted right AFTER OP_FORPREP, this should fit in a Byte.
    size_t offset = current_chunk(self)->count - prep - LUA_OPSIZE_BYTE;
    current_chunk(self)->code[prep] = (Byte)offset;
    return emit_jump(self, OP_FJMP);
}

static size_t emit_for_increment(Compiler *self, DWord iter, size_t loopstart) {
    // Hacky but we need this in order to keep our compiler single-pass.
    // For the first iteration we immediately jump OVER increment expression.
    size_t bodyjump = emit_jump(self, OP_JMP);
    size_t incrstart = current_chunk(self)->count;

    emit_bytes(self, OP_GETLOCAL, iter);
    emit_bytes(self, OP_GETLOCAL, iter + 2);
    emit_byte(self, OP_ADD);
    emit_bytes(self, OP_SETLOCAL, iter);
    

    // Strange but this is what we have to do to evaluate the increment AFTER.
    emit_loop(self, loopstart);
    patch_jump(self, bodyjump);
    return incrstart;
}

/**
 * III:23.4     For Statements
 *
 * For now we only support numeric loops with Lua's semantics.
 *
 * VISUALIZATION:
 *
 * - compile and evaluate initializer clause, push to local[0]
 * - compile and evaluate condition expression, push to local[1]
 * - compile and evaluate increment expression, push to local[2]
 *
 *        FORPREP:
 *            assert isnumber(local[0]) # sp - 3
 *            assert isnumber(local[1]) # sp - 2
 *            assert isnumber(local[2]) # sp - 1
 *            assert local[2] != 0      # Disallow for sanity
 *            if (local[2] > 0)         # Change FOR_CONDITION's 3rd operation.
 *              comparison is OP_GT
 *            else
 *              comparison is OP_LT
 *
 *        FOR_CONDITION:
 *            OP_GETLOCAL 0 <--+        # local[1], "iterator"
 *            OP_GETLOCAL 1    |        # local[0], "condition"
 *            OP_LT            |        # May be modified by OP_FORPREP
 *            OP_NOT           |        # local[1] >= local[0] ?
 * +--------- OP_FJMP          |        # goto FOR_END
 * |          OP_POP           |        # expression of for loop condition
 * |  +-----  OP_JMP           |        # goto FOR_BODY
 * |  |                        |
 * |  |  FOR_INCREMENT:        |
 * |  |       OP_GETLOCAL 0 <--|--+     # local[2], "iterator"
 * |  |       OP_GETLOCAL 2    |  |     # local[0], "increment"
 * |  |       OP_ADD           |  |     # stack[-1] = local[2] + local[0]
 * |  |       OP_SETLOCAL 0    |  |     # local[0] = stack[-1], implicit pop
 * |  |       OP_LOOP ---------+  |     # goto FOR_CONDITION
 * |  |                           |
 * |  +--> FOR_BODY:              |
 * |          ...                 |
 * |          OP_LOOP ------------+     # goto FOR_INCREMENT
 * |
 * |       FOR_END:
 * +--------> OP_POP                    # expression of for loop condition
 *            OP_NPOP     3             # pop local[0], local[1], local[2]
 *            OP_RETURN
 */
static void for_statement(Compiler *self) {
    begin_scope(self);

    // Push iterator, condition expression, and increment as local variables.
    const Token _iter = for_initializer(self);
    DWord iter        = for_limit(self, &_iter); // Index into locals array.
    size_t prep       = for_increment(self);    // Index of OP_FORPREP's operand.

    size_t loopstart = current_chunk(self)->count;
    size_t exitjump  = emit_for_limit(self, iter, prep);
    emit_byte(self, OP_POP); // Cleanup condition expression.

    // Since we need to do the increment expression last, we have to jump over.
    loopstart = emit_for_increment(self, iter, loopstart);
    consume_token(self->lex, TK_DO, "Expected 'do' after 'for' clause");

    // This creates a new scope but given our resolution rules it's probably ok.
    doblock(self);
    emit_loop(self, loopstart);
    patch_jump(self, exitjump);
    emit_byte(self, OP_POP);
    end_scope(self);
}

/**
 * III:23.2     If Statements
 *
 * Assumes that the 'then' token was just consumed. Starts a new block and then
 * compiles all declarations/statements inside of it until we hit 'else', 'end'
 * or EOF. Once any of those delimiters is reached we end the scope.
 *
 * NOTE:
 *
 * This should ONLY ever be called by `if_statement()`!
 *
 * By itself, though, it won't consume any of its delimiters. Such behavior is
 * the responsibility of the caller `if_statement()`.
 */
static void thenblock(Compiler *self) {
    begin_scope(self);
    while (!check_token(self->lex, TK_ELSEIF, TK_ELSE, TK_END, TK_EOF)) {
        declaration(self);
    }
    end_scope(self);
}

/**
 * III:23.2     If Statements
 *
 * Assumes we just consumed an 'else' token.  Similar to `thenblock()` except
 * we don't check for an 'else' token, because we already have it!
 */
static void elseblock(Compiler *self) {
    begin_scope(self);
    while (!check_token(self->lex, TK_END, TK_EOF)) {
        declaration(self);
    }
    end_scope(self);
}

/**
 * III:23.2     If Statements
 *
 * Assumes we already consumed the 'if' token and it's now the lex's previous.
 *
 * In order to do control flow with lone 'if' statements (no 'else') we need to
 * make use of a technique called 'backpatching'.
 *
 * Basically we emit a jump but we fill it with dummy values for how far to jump
 * at the moment. We keep the address of the jump instruction in memory for later.
 *
 * We then compile the statement/s inside the 'then' branch, once we're done with
 * that the current chunk's count indicates how far relative to the jump opcode
 * we need to, you know, jump.
 */
static void if_statement(Compiler *self, bool iselif) {
    LexState *lex = self->lex;
    // Compile the 'if'/'elseif' condition code.
    expression(self);
    consume_token(lex, TK_THEN, "Expected 'then' after 'if'/'elseif' condition");

    // Instruction "address" of the jump (after 'then') so we can patch it later.
    // We need to determine how big the 'then' branch is first.
    // This will jump OVER the 'then' block if falsy.
    size_t thenjump = emit_jump(self, OP_FJMP);
    emit_byte(self, OP_POP); // Pop result of expression for condition (truthy)
    thenblock(self);

    // After the then branch, we need to jump over the else branch in order to
    // avoid falling through into it.
    // This will jump OVER any succeeding 'elseif' and 'else' blocks.
    size_t elsejump = emit_jump(self, OP_JMP);

    // Chunk's current count - thenjump = how far to jump if false.
    // Also considers the elsejump in its count so we patch AFTER emitting that.
    patch_jump(self, thenjump);
    emit_byte(self, OP_POP); // Pop result of expression for condition (falsy)

    // Recursively compile 1 or more 'elseif' statments so that we emit jumps
    // to the evaluation of their conditions.
    // This is vulnerable to a stack overflow...
    if (match_token(lex, TK_ELSEIF)) {
        if_statement(self, true);
    }

    // Finally, if elseif recurse ends, we can check for this.
    // Remember that 'else' is optional.
    if (match_token(lex, TK_ELSE)) {
        elseblock(self);
    }
    patch_jump(self, elsejump);

    // We don't want to check for these in recursive calls when compiling and
    // patching an 'elseif' because they'll unwind eventually to end the primary
    // caller stack frame.
    if (!iselif) {
        consume_token(lex, TK_END, "Expected 'end' after 'if' statement");
        match_token(lex, TK_SEMICOL);
    }
}

/**
 * III:24.6     Return Statements
 *
 * Assumes we just consumed the 'return' token.
 *
 * NOTE:
 *
 * Although we check for an 'end' token immediately following the 'return', we
 * do not consume it. That is the responsibility of `function()`.
 */
static void return_statement(Compiler *self) {
    LexState *lex = self->lex;
    // Lua allows this actually! Especially for modules loaded via 'require'.
    if (self->type == FNTYPE_SCRIPT) {
        compiler_error(self, "Cannot return from top-level code");
    }
    // Not immediately followed by 'end' so might have an expression to resolve.
    if (!check_token(lex, TK_END)) {
        // Write the instructions needed to resolve the return value.
        expression(self);
        emit_byte(self, OP_RETURN);
    } else {
        // Emit OP_NIL and OP_RETURN if no return value is specified.
        emit_return(self);
    }
    match_token(lex, TK_SEMICOL);
}

/**
 * III:23.3     While Statements
 *
 * While statements are a bit of work because we need to jump backward.
 *
 * VISUALIZATION:
 *
 *      condition expression <--+
 * +--- OP_FJMP                 |
 * |    OP_POP                  |
 * |    body statement          |
 * |    OP_LOOP              ---+
 * +--> OP_POP
 *      continue...
 */
static void while_statement(Compiler *self) {
    // Save address of the beginning of the loop, right before the condition.
    size_t loopstart = current_chunk(self)->count;
    expression(self);
    consume_token(self->lex, TK_DO, "Expected 'do' after 'while' condition");

    // Save the address of the jump opcode which will exit out of the loop.
    size_t exitjump = emit_jump(self, OP_FJMP);

    emit_byte(self, OP_POP); // Condition expression cleanup (truthy)
    doblock(self);
    emit_loop(self, loopstart);
    patch_jump(self, exitjump);
    emit_byte(self, OP_POP); // Condition expression cleanup (falsy)
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
 * `staement()` which eventually calls `variable()`.
 */
static void declaration(Compiler *self) {
    if (match_token(self->lex, TK_LOCAL)) {
        if (match_token(self->lex, TK_FUNCTION)) {
            // 'local function' identifier block 'end'
            function_declaration(self, true);
        } else {
            // 'local' identifier ['=' expression]
            variable_declaration(self);
        }
    } else if (match_token(self->lex, TK_FUNCTION)) {
        // 'function' identifier block 'end'
        function_declaration(self, false);
    } else {
        statement(self);
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
 * and in turn defer responsibility down to `variable_statement()`.
 *
 * Unfortunately this allows us to nest assignments which is not consistent
 * with Lua's official design.
 *
 * NOTE:
 *
 * I've since updated the semantics so that assignments CANNOT be nested.
 */
static void statement(Compiler *self) {
    LexState *lex = self->lex;
    if (match_token(lex, TK_BREAK)) {
        compiler_error(self, "Breaks not yet implemented");
    } else if (match_token(lex, TK_IF)) {
        if_statement(self, false);
    } else if (match_token(lex, TK_FOR)) {
        for_statement(self);
    } else if (match_token(lex, TK_ELSEIF, TK_ELSE)) {
        compiler_error(self, "No parent 'if' statement");
    } else if (match_token(lex, TK_IDENT)) {
        variable_statement(self);
    } else if (match_token(lex, TK_RETURN)) {
        return_statement(self);
    } else if (match_token(lex, TK_WHILE)) {
        while_statement(self);
    } else if (match_token(lex, TK_DO)) {
        doblock(self);
    } else {
        compiler_error_current(self, "No statement found");
    }

    // Disallow lone/trailing semicolons that weren't consumed by statements.
    if (match_token(lex, TK_SEMICOL)) {
        compiler_error(self, "Unexpected symbol");
    }
}

Proto *compile_bytecode(Compiler *self) {
    // I'm worried about using a local variable here since `longjmp()` might
    // restore registers to the "snapshot" when `setjmp()` was called.
    // The `volatile` keyword would be needed, but then that might have a
    // performance impact: https://stackoverflow.com/a/54568820
    LexState *lex = self->lex;

    // setjmp returns 0 on init, else a nonzero when called by longjmp.
    // Be VERY careful not to longjmp from functions with heap allocations since
    // we won't do most forms of cleanup.
    if (setjmp(*lex->errjmp) == 0) {
        begin_scope(self); // File-scope/REPL line scope is its own block scope.
        next_token(self->lex);
        while (!match_token(lex, TK_EOF)) {
            declaration(self);
        }
        end_scope(self);
    }
    Proto *function = end_compiler(self);
    return (lex->haderror) ? NULL : function;
}
