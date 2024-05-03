#include "compiler.h"
#include "parser.h"
#include "vm.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

void init_compiler(Compiler *self, Lexer *lexer, VM *vm) {
    self->lexer       = lexer;
    self->vm          = vm;
    self->chunk       = NULL;
    self->local_count = 0;
    self->scope_depth = 0;
    self->stack_total = 0;
    self->stack_usage = 0;
    self->prev_opcode = OP_RETURN;
}

// Will also set `prev_opcode`. Pass `delta` only when `op` has a variable stack
// effect, as indicated by `VAR_DELTA`.
static void adjust_stackinfo(Compiler *self, OpCode op, int delta) {
    Lexer  *lexer = self->lexer;
    OpInfo  info  = get_opinfo(op);
    int     push  = (info.push == VAR_DELTA) ? delta : info.push;
    int     pop   = (info.pop  == VAR_DELTA) ? delta : info.pop;

    // If both push and pop are VAR_DELTA then something is horribly wrong.
    self->stack_usage += push - pop;
    self->prev_opcode = op;
    if (self->stack_usage > MAX_STACK) {
        lexerror_at_consumed(lexer, "Function uses too many stack slots");
    }
    if (self->stack_usage > self->stack_total) {
        self->stack_total = self->stack_usage;
    }
}

// EMITTING BYTECODE ------------------------------------------------------ {{{1

static void emit_byte(Compiler *self, Byte data) {
    Alloc *alloc = &self->vm->alloc;
    Lexer *lexer = self->lexer;
    int    line  = lexer->consumed.line;
    write_chunk(current_chunk(self), data, line, alloc);
}

Chunk *current_chunk(Compiler *self) {
    return self->chunk;
}

void emit_opcode(Compiler *self, OpCode op) {
    emit_byte(self, op);
    adjust_stackinfo(self, op, 0);
}

/**
 * @brief   Optimize consecutive opcodes with a variable delta. Assumes we
 *          already know we want to adjust_vardelta an instruction/set thereof.
 *
 * @note    Assumes that the variable delta is always at the top of the bytecode.
 */
static bool adjust_vardelta(Compiler *self, OpCode op, Byte arg) {
    Chunk *chunk = current_chunk(self);
    Byte  *code  = chunk->code + (chunk->len - 1);
    int    delta = arg;

    switch (op) {
    // Instead of emitting a separate POP we can just add to the variable delta
    // of SETTABLE.
    case OP_POP:
        if (self->prev_opcode == OP_SETTABLE) {
            return adjust_vardelta(self, OP_SETTABLE, arg);
        }
        break;
    // -1 since each OP_CONCAT assumes at least 2 args, so folding 2+ will
    // result in 1 less explicit pop as it's now been combined.
    case OP_CONCAT:
        delta -= 1;
        break;
    default:
        break;
    }

    // This branch is down here in case of POP needing to optimize SETTABLE.
    if (self->prev_opcode != op) {
        return false;
    }

    // Adjusting would cause overflow? (Assumes `int` is bigger than `Byte`)
    if (cast(int, *code) + delta > cast(int, MAX_BYTE)) {
        return false;
    }
    *code += delta;

    // Do this here mainly for the case of POP delegating to SETTABLE. This is
    // to prevent the wrong prev opcode from being set in such cases.
    adjust_stackinfo(self, op, delta);
    return true;
}

void emit_oparg1(Compiler *self, OpCode op, Byte arg) {
    switch (op) {
    case OP_POP:
    case OP_CONCAT: // Fall through
    case OP_NIL:
        if (adjust_vardelta(self, op, arg)) {
            return;
        }
        // Fall through
    default:
        emit_byte(self, op);
        emit_byte(self, arg);
        adjust_stackinfo(self, op, arg);
        break;
    }
}

void emit_oparg2(Compiler *self, OpCode op, Byte2 arg) {
    emit_byte(self, op);
    emit_byte(self, encode_byte2_msb(arg));
    emit_byte(self, encode_byte2_lsb(arg));

    switch (op) {
    case OP_SETTABLE:
        // arg A is msb, arg B is lsb, and SETTABLE has variable delta arg B
        adjust_stackinfo(self, op, encode_byte2_lsb(arg));
        break;
    default:
        break;
    }
}

void emit_oparg3(Compiler *self, OpCode op, Byte3 arg) {
    // We assume that no opcode with a 3-byte argument has a variable delta.
    emit_opcode(self, op);
    emit_byte(self, encode_byte3_msb(arg));
    emit_byte(self, encode_byte3_mid(arg));
    emit_byte(self, encode_byte3_lsb(arg));
}

void emit_identifier(Compiler *self) {
    Lexer *lexer = self->lexer;
    Token *ident = &lexer->consumed;
    emit_oparg3(self, OP_CONSTANT, identifier_constant(self, ident));
}

void emit_return(Compiler *self) {
    emit_opcode(self, OP_RETURN);
}

// 1}}} ------------------------------------------------------------------------

int make_constant(Compiler *self, const TValue *value) {
    Alloc *alloc = &self->vm->alloc;
    Lexer *lexer = self->lexer;
    int    index = add_constant(current_chunk(self), value, alloc);
    if (index + 1 > MAX_CONSTS) {
        lexerror_at_consumed(lexer, "Too many constants in current chunk");
    }
    return index;
}

void emit_constant(Compiler *self, const TValue *value) {
    int index = make_constant(self, value);
    emit_oparg3(self, OP_CONSTANT, index);
}

void emit_variable(Compiler *self, const Token *ident) {
    int  arg     = resolve_local(self, ident);
    bool islocal = (arg != -1);

    // Global vs. local operands have different sizes.
    if (islocal) {
        emit_oparg1(self, OP_GETLOCAL, arg);
    } else {
        emit_oparg3(self, OP_GETGLOBAL, identifier_constant(self, ident));
    }
}

int identifier_constant(Compiler *self, const Token *ident) {
    TValue wrapper = make_string(copy_string(self->vm, &ident->view));
    return make_constant(self, &wrapper);
}

void end_compiler(Compiler *self) {
    emit_return(self);
#ifdef DEBUG_PRINT_CODE
    printf("[STACK USAGE]:\n"
           "NET:    %i\n"
           "MOST:   %i\n",
           self->stack_usage,
           self->stack_total);
    printf("\n");
    disassemble_chunk(current_chunk(self));
#endif
}

void begin_scope(Compiler *self) {
    Lexer *lexer = self->lexer;
    if ((self->scope_depth += 1) > MAX_LEVELS) {
        lexerror_at_lookahead(lexer, "Function uses too many syntax levels");
    }
}

void end_scope(Compiler *self) {
    self->scope_depth--;

    int popped = 0;
    while (self->local_count > 0) {
        if (self->locals[self->local_count - 1].depth <= self->scope_depth) {
            break;
        }
        popped++;
        self->local_count--;
    }
    // Don't waste 2 bytes if nothing to pop
    if (popped > 0) {
        emit_oparg1(self, OP_POP, popped);
    }
}

void compile(Compiler *self, const char *input, Chunk *chunk) {
    Lexer *lexer = self->lexer;
    self->chunk  = chunk;
    init_lexer(lexer, input, self->vm);
    next_token(lexer);

    begin_scope(self); // Script/REPL can pop its own top-level locals
    while (!match_token(lexer, TK_EOF)) {
        declaration(self);
    }
    expect_token(lexer, TK_EOF, "Expected end of expression");
    end_scope(self);
    end_compiler(self);
}

// LOCAL VARIABLES -------------------------------------------------------- {{{1

static bool identifiers_equal(const Token *a, const Token *b) {
    const StrView *s1 = &a->view;
    const StrView *s2 = &b->view;
    return s1->len == s2->len && cstr_eq(s1->begin, s2->begin, s1->len);
}

int resolve_local(Compiler *self, const Token *ident) {
    for (int i = self->local_count - 1; i >= 0; i--) {
        const Local *local = &self->locals[i];
        // If using itself in initializer, continue to resolve outward.
        if (local->depth != -1 && identifiers_equal(ident, &local->ident)) {
            return i;
        }
    }
    return -1;
}

void add_local(Compiler *self, const Token *ident) {
    if (self->local_count + 1 > MAX_LOCALS) {
        lexerror_at_consumed(self->lexer,
            "More than " stringify(MAX_LOCALS) " local variables reached");
    }
    Local *local = &self->locals[self->local_count++];
    local->ident = *ident;
    local->depth = -1;
}

void init_local(Compiler *self) {
    Lexer *lexer = self->lexer;
    Token *ident = &lexer->consumed;

    // Detect variable shadowing in the same scope.
    for (int i = self->local_count - 1; i >= 0; i--) {
        const Local *local = &self->locals[i];
        // Have we hit an outer scope?
        if (local->depth != -1 && local->depth < self->scope_depth) {
            break;
        }
        if (identifiers_equal(ident, &local->ident)) {
            lexerror_at_consumed(lexer, "Shadowing of local variable");
        }
    }
    add_local(self, ident);
}

void define_locals(Compiler *self, int count) {
    int limit = self->local_count;
    for (int i = count; i > 0; i--) {
        self->locals[limit - i].depth = self->scope_depth;
    }
}

// 1}}} ------------------------------------------------------------------------
