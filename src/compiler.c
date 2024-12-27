/// local
#include "compiler.h"
#include "vm.h"
#include "parser.h"
#include "debug.h"

/// standard
#include <stdio.h>
#include <string.h>

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

    // We assume that variable delta instructions are affected by argument A.
    int n_push = (info.push_count == -1) ? instr_get_A(inst) : info.push_count;
    int n_pop  = (info.pop_count  == -1) ? instr_get_A(inst) : info.pop_count;

    /**
     * @note 2024-12-27
     *      This is ugly but I cannot be bothered to change all the instructions
     *      to somehow have a uniform argument for variable deltas.
     */
    if (op == OP_SETTABLE) {
        n_pop = instr_get_C(inst);
    }

    printf("%-12s: push %i, pop %i\n", info.name, n_push, n_pop);
    self->stack_usage += n_push;
    self->stack_usage -= n_pop;
}

static void
emit_instruction(Compiler *self, Instruction inst)
{
    lulu_VM *vm   = self->vm;
    int      line = self->parser->consumed.line;
    chunk_write(vm, current_chunk(self), inst, line);
    adjust_stack_usage(self, inst);
}

void
compiler_emit_opcode(Compiler *self, OpCode op)
{
    emit_instruction(self, instr_make(op, 0, 0, 0));
}

void
compiler_emit_return(Compiler *self)
{
    compiler_emit_opcode(self, OP_RETURN);
}

byte3
compiler_make_constant(Compiler *self, const Value *value)
{
    lulu_VM *vm    = self->vm;
    isize    index = chunk_add_constant(vm, current_chunk(self), value);
    if (index > LULU_MAX_CONSTANTS) {
        parser_error_consumed(self->parser, "Too many constants in one chunk.");
        return 0;
    }
    return cast(byte3)index;
}

void
compiler_emit_constant(Compiler *self, const Value *value)
{
    byte3 index = compiler_make_constant(self, value);
    emit_instruction(self, instr_make_byte3(OP_CONSTANT, index));
}

void
compiler_emit_string(Compiler *self, const Token *token)
{
    Value tmp;
    value_set_string(&tmp, ostring_new(self->vm, token->start, token->len));
    compiler_emit_constant(self, &tmp);
}

void
compiler_emit_number(Compiler *self, lulu_Number n)
{
    Value tmp;
    value_set_number(&tmp, n);
    compiler_emit_constant(self, &tmp);
}

static bool
folded_instruction(Compiler *self, Instruction inst)
{
    OpCode  op    = instr_get_op(inst);
    Chunk  *chunk = current_chunk(self);

    // Can't possibly have a previous opcode?
    if (chunk->len <= 0) {
        return false;
    }

    // Poke at the most recently (i.e: previous) written opcode.
    Instruction *ip = &chunk->code[chunk->len - 1];
    if (instr_get_op(*ip) != op) {
        return false;
    }

    // e.g. folded CONCATs always requires 1 less actual intermediate.
    int offset = 0;
    switch (op) {
    case OP_CONCAT: offset = -1;
    case OP_POP:
    case OP_NIL: {
        int prev_arg = cast(int)instr_get_A(*ip);
        int cur_arg  = cast(int)instr_get_A(inst);
        int new_arg  = prev_arg + cur_arg + offset;
        if (0 < new_arg && new_arg <= cast(int)LULU_MAX_BYTE) {
            instr_set_A(ip, cast(byte)new_arg);
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
        emit_instruction(self, inst);
    }
}

void
compiler_emit_byte3(Compiler *self, OpCode op, byte3 arg)
{
    emit_instruction(self, instr_make_byte3(op, arg));
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
    while (self->n_locals > 0 && self->locals[self->n_locals -1].depth > self->scope_depth) {
        self->n_locals--;
        n_pop++;
    }
    
    if (n_pop > 0) {
        compiler_emit_A(self, OP_POP, n_pop);
    }
}

static bool
identifiers_equal(const OString *string, const Token *token)
{
    if (string->len != token->len) {
        return false;
    }
    return memcmp(string->data, token->start, string->len) == 0;
}

void
compiler_add_local(Compiler *self, const Token *ident)
{
    if (self->n_locals >= cast(int)LULU_MAX_BYTE) {
        parser_error_consumed(self->parser, "Too many local variables in function");
        return;
    }

    // Are we shadowing?
    for (int i = self->n_locals - 1; i >= 0; i--) {
        Local *local = &self->locals[i];
        // Checking an already defined variable of an enclosing scope?
        if (local->depth != -1 && local->depth < self->scope_depth) {
            break;
        }
        if (identifiers_equal(local->name, ident)) {
            parser_error_consumed(self->parser, "Shadowing of local variable");
            return;
        }
    }

    Local *local = &self->locals[self->n_locals++];
    local->name  = ostring_new(self->vm, ident->start, ident->len);
    local->depth = UNINITIALIZED_LOCAL;
}

void
compiler_initialize_locals(Compiler *self)
{
    for (int i = self->n_locals - 1; i >= 0; i--) {
        Local *local = &self->locals[i];
        if (local->depth != UNINITIALIZED_LOCAL) {
            break;
        }
        local->depth = self->scope_depth;
    }
}

int
compiler_resolve_local(Compiler *self, const Token *ident)
{
    for (int i = self->n_locals - 1; i >= 0; i--) {
        const Local *local = &self->locals[i];
        if (local->depth != UNINITIALIZED_LOCAL && identifiers_equal(local->name, ident)) {
            return i;
        }
    }
    return UNRESOLVED_LOCAL;
}

isize
compiler_new_table(Compiler *self)
{
    isize index = current_chunk(self)->len;
    compiler_emit_opcode(self, OP_NEWTABLE);
    return index;
}

void
compiler_adjust_table(Compiler *self, isize i_code, isize n_fields)
{
    Instruction *ip = &current_chunk(self)->code[i_code];
    if (n_fields == 0) {
        return;
    }
    instr_set_byte3(ip, n_fields);
}

void
compiler_set_table(Compiler *self, int i_table, int i_key, int n_pop)
{
    emit_instruction(self, instr_make(OP_SETTABLE, i_table, i_key, n_pop));
}
