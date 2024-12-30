/// local
#include "compiler.h"
#include "vm.h"
#include "parser.h"
#include "debug.h"

/// standard
#include <stdio.h>  // printf
#include <string.h> // memcmp

void
compiler_init(lulu_VM *vm, Compiler *self)
{
    self->vm     = vm;
    self->chunk  = NULL;
    self->parser = NULL;
    self->lexer  = NULL;

    self->n_locals = 0;
    self->scope_depth = 0;
    self->stack_usage = 0;
}

/**
 * @note 2024-09-07
 *      This will get more complicated later on, hence we abstract it out now.
 */
static Chunk *
current_chunk(Compiler *self)
{
    return self->chunk;
}

static void
adjust_stack_usage(Compiler *self, Instruction inst)
{
    OpCode      op   = instr_get_op(inst);
    OpCode_Info info = LULU_OPCODE_INFO[op];

    // We assume that variable delta instructions only occur on argument A.
    int n_push = (info.n_push == -1) ? instr_get_A(inst) : info.n_push;
    int n_pop  = (info.n_pop  == -1) ? instr_get_A(inst) : info.n_pop;

    // printf("%-12s: push %i, pop %i\n", info.name, n_push, n_pop); //! DEBUG
    self->stack_usage += n_push - n_pop;
}

static void
compiler_emit_instruction(Compiler *self, Instruction inst)
{
    lulu_VM *vm   = self->vm;
    int      line = self->parser->consumed.line;
    chunk_write(vm, current_chunk(self), inst, line);
    adjust_stack_usage(self, inst);
}

void
compiler_emit_op(Compiler *self, OpCode op)
{
    compiler_emit_instruction(self, instr_make(op, 0, 0, 0));
}

void
compiler_emit_return(Compiler *self)
{
    compiler_emit_op(self, OP_RETURN);
}

byte3
compiler_make_constant(Compiler *self, const Value *value)
{
    lulu_VM *vm    = self->vm;
    int      index = chunk_add_constant(vm, current_chunk(self), value);
    if (index > cast(int)LULU_MAX_CONSTANTS) {
        parser_error_consumed(self->parser, "Too many constants in one chunk.");
        return 0;
    }
    return cast(byte3)index;
}

void
compiler_emit_constant(Compiler *self, const Value *value)
{
    byte3 index = compiler_make_constant(self, value);
    compiler_emit_instruction(self, instr_make_ABC(OP_CONSTANT, index));
}

void
compiler_emit_string(Compiler *self, const Token *token)
{
    Value tmp;
    value_set_string(&tmp, ostring_new(self->vm, token->start, token->len));
    compiler_emit_constant(self, &tmp);
}

void
compiler_emit_number(Compiler *self, Number n)
{
    Value tmp;
    value_set_number(&tmp, n);
    compiler_emit_constant(self, &tmp);
}

/**
 * @todo 2024-12-27:
 *      When folding overflows the previous argument A, can we split the difference
 *      and write that into `inst` for the caller to see?
 */
static bool
folded_instruction(Compiler *self, Instruction inst)
{
    OpCode  cur_op = instr_get_op(inst);
    Chunk  *chunk  = current_chunk(self);

    // Can't possibly have a previous opcode?
    if (chunk->len <= 0) {
        return false;
    }

    // Poke at the most recently (i.e: previous) written opcode.
    Instruction *ip      = &chunk->code[chunk->len - 1];
    OpCode       prev_op = instr_get_op(*ip);
    if (prev_op != cur_op) {
        // Account for special case
        if (!(prev_op == OP_SET_TABLE && cur_op == OP_POP)) {
            return false;
        }
    }

    // e.g. folded CONCATs always requires 1 less actual intermediate.
    int offset = 0;
    switch (cur_op) {
    case OP_CONCAT: offset = -1;
    case OP_POP:
    case OP_NIL: {
        int prev_arg = cast(int)instr_get_A(*ip);
        int cur_arg  = cast(int)instr_get_A(inst);
        int new_arg  = prev_arg + cur_arg + offset;
        if (0 < new_arg && new_arg <= cast(int)LULU_MAX_BYTE) {
            // @warning implicit cast: differently sized integers
            // However, we did do the above check to ensure 'new_arg' fits.
            instr_set_A(ip, new_arg);

            // We assume that multiple of the same instruction evaluates to the
            // same net stack usage.
            adjust_stack_usage(self, inst);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void
compiler_emit_A(Compiler *self, OpCode op, byte a)
{
    Instruction inst = instr_make_A(op, a);
    if (!folded_instruction(self, inst)) {
        compiler_emit_instruction(self, inst);
    }
}

void
compiler_emit_ABC(Compiler *self, OpCode op, byte3 arg)
{
    compiler_emit_instruction(self, instr_make_ABC(op, arg));
}

void
compiler_emit_nil(Compiler *self, int n_nil)
{
    compiler_emit_A(self, OP_NIL, cast(byte)n_nil);
}

// @todo 2024-12-27: Check for overflow before casting to `byte`?
void
compiler_emit_pop(Compiler *self, int n_pop)
{
    compiler_emit_A(self, OP_POP, cast(byte)n_pop);
}

void
compiler_emit_lvalues(Compiler *self, LValue *last)
{
    LValue *iter      = last;
    int     n_cleanup = 0;    // How many table/key pairs to pop at the end?
    while (iter) {
        switch (iter->type) {
        case LVALUE_GLOBAL:
            compiler_emit_ABC(self, OP_SET_GLOBAL, iter->global);
            break;
        case LVALUE_LOCAL:
            compiler_emit_A(self, OP_SET_LOCAL, iter->local);
            break;
        case LVALUE_TABLE:
            compiler_set_table(self, iter->i_table, iter->i_key, iter->n_pop);
            n_cleanup += 2; // Get rid of remaining table and key
            break;
        }
        iter = iter->prev;
    }

    if (n_cleanup > 0) {
        compiler_emit_pop(self, n_cleanup);
    }
}

void
compiler_emit_lvalue_parent(Compiler *self, LValue *lvalue)
{
    // SET(GLOBAL|LOCAL) only apply to the primary (parent) table. Everything
    // else is guaranteed to be a field of this table or its subtables.
    switch (lvalue->type) {
    case LVALUE_GLOBAL:
        compiler_emit_ABC(self, OP_GET_GLOBAL, lvalue->global);
        break;
    case LVALUE_LOCAL:
        compiler_emit_A(self, OP_GET_LOCAL, lvalue->local);
        break;
    case LVALUE_TABLE:
        // For this case, a table and a key have been emitted right before.
        compiler_emit_op(self, OP_GET_TABLE);
        break;
    }
}

void
compiler_end(Compiler *self)
{
    compiler_emit_return(self);
#ifdef LULU_DEBUG_PRINT
    printf("=== STACK USAGE ===\n"
           "Net: %i\n"
           "\n",
           self->stack_usage);
    debug_disasssemble_chunk(current_chunk(self));
#endif
}

void
compiler_compile(Compiler *self, cstring input, Chunk *chunk)
{
    Lexer  lexer;
    Parser parser;

    /**
     * @note 2024-10-30
     *      Could be in 'compiler_init()'...
     */
    self->chunk  = chunk;
    self->lexer  = &lexer;
    self->parser = &parser;
    parser_init(&parser, self, &lexer);
    lexer_init(self->vm, &lexer, chunk->filename, input);

    parser_advance_token(&parser);

    compiler_begin_scope(self);
    while (!parser_match_token(&parser, TOKEN_EOF)) {
        parser_declaration(&parser);
    }
    compiler_end_scope(self);
    compiler_end(self);
}

void
compiler_begin_scope(Compiler *self)
{
    self->scope_depth++;
}

void
compiler_end_scope(Compiler *self)
{
    self->scope_depth--;

    // Remove this scope's local variables.
    int n_pop = 0;
    while (self->n_locals > 0 && self->locals[self->n_locals - 1].depth > self->scope_depth) {
        self->n_locals--;
        n_pop++;
    }

    if (n_pop > 0) {
        compiler_emit_pop(self, n_pop);
    }
}

static bool
identifiers_equal(const OString *string, const Token *token)
{
    if (string->len != token->len) {
        return false;
    }
    return memcmp(string->data, token->start, token->len) == 0;
}

void
compiler_add_local(Compiler *self, const Token *ident)
{
    if (self->n_locals >= cast(int)LULU_MAX_BYTE) {
        parser_error_consumed(self->parser, "Too many local variables in function");
        return;
    }

    // Are we shadowing?
    Local *locals = self->locals;
    for (int i = self->n_locals - 1; i >= 0; i--) {
        Local *local = &locals[i];
        // Checking an already defined variable of an enclosing scope?
        if (local->depth != UNINITIALIZED_LOCAL && local->depth < self->scope_depth) {
            break;
        }
        if (identifiers_equal(local->name, ident)) {
            parser_error_consumed(self->parser, "Shadowing of local variable");
            return;
        }
    }

    Local *local = &locals[self->n_locals++];
    local->name  = ostring_new(self->vm, ident->start, ident->len);
    local->depth = UNINITIALIZED_LOCAL;
}

void
compiler_initialize_locals(Compiler *self)
{
    Local *locals = self->locals;
    for (int i = self->n_locals - 1; i >= 0; i--) {
        Local *local = &locals[i];
        if (local->depth != UNINITIALIZED_LOCAL) {
            break;
        }
        local->depth = self->scope_depth;
    }
}

int
compiler_resolve_local(Compiler *self, const Token *ident)
{
    Local *locals = self->locals;
    for (int i = self->n_locals - 1; i >= 0; i--) {
        const Local *local = &locals[i];
        if (local->depth != UNINITIALIZED_LOCAL && identifiers_equal(local->name, ident)) {
            return i;
        }
    }
    return UNRESOLVED_LOCAL;
}

int
compiler_new_table(Compiler *self)
{
    int index = current_chunk(self)->len;
    compiler_emit_op(self, OP_NEW_TABLE);
    return index;
}

void
compiler_adjust_table(Compiler *self, int i_code, int n_hash, int n_array)
{
    Instruction *ip = &current_chunk(self)->code[i_code];
    instr_set_A(ip, n_hash);
    instr_set_B(ip, n_array);

    if (n_array > 0) {
        compiler_emit_instruction(self, instr_make(OP_SET_ARRAY, n_array, i_code, 0));
    }
}

void
compiler_set_table(Compiler *self, int i_table, int i_key, int n_pop)
{
    compiler_emit_instruction(self, instr_make(OP_SET_TABLE, n_pop, i_table, i_key));
}
