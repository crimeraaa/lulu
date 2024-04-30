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
    Token *token = &lexer->consumed;
    write_chunk(current_chunk(self), data, token->line, alloc);
}

Chunk *current_chunk(Compiler *self) {
    return self->chunk;
}

void emit_opcode(Compiler *self, OpCode op) {
    emit_byte(self, op);
    adjust_stackinfo(self, op, 0);
}

void emit_oparg1(Compiler *self, OpCode op, Byte arg) {
    Chunk  *chunk = current_chunk(self);
    int     len   = chunk->len;

    switch (op) {
    case OP_CONCAT:
    case OP_NIL:
    case OP_POP:
        // Optimize consecutive opcodes with a variable delta.
        if (self->prev_opcode == op) {
            // -1 since each OP_CONCAT assumes at least 2 args, so folding 2+
            // will result in 1 less explicit pop as it's now been combined.
            chunk->code[len - 1] += (op == OP_CONCAT) ? arg - 1 : arg;
            break;
        } else {
            goto no_optimization;
        }
    no_optimization:
    default:
        emit_byte(self, op);
        emit_byte(self, arg);
        break;
    }

    adjust_stackinfo(self, op, arg);
}

void emit_oparg2(Compiler *self, OpCode op, Byte2 arg) {
    emit_opcode(self, op);
    emit_byte(self, encode_byte2_msb(arg));
    emit_byte(self, encode_byte2_lsb(arg));
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

int identifier_constant(Compiler *self, const Token *name) {
    TString *ident = copy_string(self->vm, name->start, name->len);
    TValue wrapper = make_string(ident);
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
        lexerror_at_token(lexer, "Function uses too many syntax levels");
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
    consume_token(lexer, TK_EOF, "Expected end of expression");
    end_scope(self);
    end_compiler(self);
}

// LOCAL VARIABLES -------------------------------------------------------- {{{1

static bool identifiers_equal(const Token *a, const Token *b) {
    return (a->len == b->len) && cstr_equal(a->start, b->start, a->len);
}

int resolve_local(Compiler *self, const Token *name) {
    for (int i = self->local_count - 1; i >= 0; i--) {
        const Local *local = &self->locals[i];
        // If using itself in initializer, continue to resolve outward.
        if (local->depth != -1 && identifiers_equal(name, &local->name)) {
            return i;
        }
    }
    return -1;
}

void add_local(Compiler *self, const Token *name) {
    if (self->local_count + 1 > MAX_LOCALS) {
        lexerror_at_consumed(self->lexer,
            "More than " stringify(MAX_LOCALS) " local variables reached");
    }
    Local *local = &self->locals[self->local_count++];
    local->name  = *name;
    local->depth = -1;
}

void init_local(Compiler *self) {
    Lexer *lexer = self->lexer;
    Token *name  = &lexer->consumed;

    // Detect variable shadowing in the same scope.
    for (int i = self->local_count - 1; i >= 0; i--) {
        const Local *local = &self->locals[i];
        // Have we hit an outer scope?
        if (local->depth != -1 && local->depth < self->scope_depth) {
            break;
        }
        if (identifiers_equal(name, &local->name)) {
            lexerror_at_consumed(lexer, "Shadowing of local variable");
        }
    }
    add_local(self, name);
}

void define_locals(Compiler *self, int count) {
    int limit = self->local_count;
    for (int i = count; i > 0; i--) {
        self->locals[limit - i].depth = self->scope_depth;
    }
}

// 1}}} ------------------------------------------------------------------------
