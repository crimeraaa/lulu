#include "compiler.h"
#include "parser.h"
#include "vm.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

void init_compiler(Compiler *self, Lexer *lexer, VM *vm) {
    self->lexer      = lexer;
    self->vm         = vm;
    self->chunk      = NULL;
    self->localcount = 0;
    self->scopedepth = 0;
    self->stacktotal = 0;
    self->stackusage = 0;
}

static void adjust_stackinfo(Compiler *self, int push, int pop) {
    Lexer *lexer = self->lexer;
    self->stackusage += push - pop;
    if (self->stackusage > MAX_STACK) {
        lexerror_at_consumed(lexer, "Function uses too many stack slots");
    }
    if (self->stackusage > self->stacktotal) {
        self->stacktotal = self->stackusage;
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
    OpInfo *info = get_opinfo(op);
    emit_byte(self, op);
    adjust_stackinfo(self, info->push, info->pop);
}

void emit_oparg1(Compiler *self, OpCode op, Byte arg) {
    OpInfo *info = get_opinfo(op);

    switch (op) {
    case OP_NIL: {
        Chunk *chunk = current_chunk(self);
        int    len   = chunk->len;
        // Minor optimization: combine consecutive OP_NIL's.
        if (len >= 2 && chunk->code[len - 2] == OP_NIL) {
            chunk->code[len - 1] += arg;
            break;
        }
        // Otherwise fall through to default case.
    }
    default:
        emit_byte(self, op);
        emit_byte(self, arg);
        break;
    }

    // If both push and pop are VAR_DELTA then something is horribly wrong.
    adjust_stackinfo(self,
                    (info->push == VAR_DELTA) ? arg : info->push,
                    (info->pop  == VAR_DELTA) ? arg : info->pop);
}

void emit_oparg2(Compiler *self, OpCode op, Byte2 arg) {
    emit_byte(self, op);
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

void emit_return(Compiler *self) {
    emit_opcode(self, OP_RETURN);
}

void emit_gettable(Compiler *self, Assignment *list, int *nest) {
    // Did we have a field/index previously? If so, resolve the table.
    if (list->prev != NULL && list->prev->type >= ASSIGN_FIELD) {
        emit_opcode(self, OP_GETTABLE);
        if (nest != NULL) {
            *nest += 1;
        }
    }
}

void emit_fields(Compiler *self, Assignment *list, int *nest) {
    if (list == NULL) {
        return;
    }
    // Recurse in such a way that the previous-most (i.e. oldest) Assignment*
    // has its operations resolved first, mainly an OP_GET(GLOBAL|LOCAL).
    emit_fields(self, list->prev, nest);
    switch (list->type) {
    case ASSIGN_INDEX:
        // Key is implicit thanks to the value pushed by `expression()`.
        emit_gettable(self, list, nest);
        break;
    case ASSIGN_FIELD:
        emit_gettable(self, list, nest);
        emit_oparg3(self, OP_CONSTANT, list->arg);
        break;
    case ASSIGN_GLOBAL:
        emit_oparg3(self, OP_GETGLOBAL, list->arg);
        break;
    case ASSIGN_LOCAL:
        emit_oparg1(self, OP_GETLOCAL, list->arg);
        break;
    }
}

// 1}}} ------------------------------------------------------------------------

int make_constant(Compiler *self, const TValue *value) {
    Lexer *lexer = self->lexer;
    int index = add_constant(current_chunk(self), value, &self->vm->alloc);
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
           self->stackusage,
           self->stacktotal);
    printf("\n");
    disassemble_chunk(current_chunk(self));
#endif
}

void begin_scope(Compiler *self) {
    Lexer *lexer = self->lexer;
    if ((self->scopedepth += 1) > MAX_LEVELS) {
        lexerror_at_token(lexer, "Function uses too many syntax levels");
    }
}

void end_scope(Compiler *self) {
    self->scopedepth--;

    int popped = 0;
    while (self->localcount > 0) {
        if (self->locals[self->localcount - 1].depth <= self->scopedepth) {
            break;
        }
        popped++;
        self->localcount--;
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
    for (int i = self->localcount - 1; i >= 0; i--) {
        const Local *local = &self->locals[i];
        // If using itself in initializer, continue to resolve outward.
        if (local->depth != -1 && identifiers_equal(name, &local->name)) {
            return i;
        }
    }
    return -1;
}

void add_local(Compiler *self, const Token *name) {
    if (self->localcount + 1 > MAX_LOCALS) {
        lexerror_at_consumed(self->lexer,
            "More than " stringify(MAX_LOCALS) " local variables reached");
    }
    Local *local = &self->locals[self->localcount++];
    local->name  = *name;
    local->depth = -1;
}

void init_local(Compiler *self) {
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

void define_locals(Compiler *self, int count) {
    int limit = self->localcount;
    for (int i = count; i > 0; i--) {
        self->locals[limit - i].depth = self->scopedepth;
    }
}

// 1}}} ------------------------------------------------------------------------
