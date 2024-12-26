/// local
#include "compiler.h"
#include "vm.h"
#include "parser.h"
#include "debug.h"

/// standard
#include <stdio.h>
#include <string.h>

void
lulu_Compiler_init(lulu_VM *vm, lulu_Compiler *self)
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
static lulu_Chunk *
current_chunk(lulu_Compiler *self)
{
    return self->chunk;
}

static void
adjust_stack_usage(lulu_Compiler *self, lulu_Instruction inst)
{
    lulu_OpCode      op   = lulu_Instruction_get_opcode(inst);
    lulu_OpCode_Info info = LULU_OPCODE_INFO[op];

    // We assume that variable delta instructions are affected by argument A.
    int n_push = (info.push_count == -1) ? lulu_Instruction_get_A(inst) : info.push_count;
    int n_pop  = (info.pop_count  == -1) ? lulu_Instruction_get_A(inst) : info.pop_count;

    /**
     * @note 2024-12-27
     *      This is ugly but I cannot be bothered to change all the instructions
     *      to somehow have a uniform argument for variable deltas.
     */
    if (op == OP_SETTABLE) {
        n_pop = lulu_Instruction_get_C(inst);
    }

    printf("%-12s: push %i, pop %i\n", info.name, n_push, n_pop);
    self->stack_usage += n_push;
    self->stack_usage -= n_pop;
}

static void
emit_instruction(lulu_Compiler *self, lulu_Instruction inst)
{
    lulu_VM *vm   = self->vm;
    int      line = self->parser->consumed.line;
    lulu_Chunk_write(vm, current_chunk(self), inst, line);
    adjust_stack_usage(self, inst);
}

void
lulu_Compiler_emit_opcode(lulu_Compiler *self, lulu_OpCode op)
{
    emit_instruction(self, lulu_Instruction_make_none(op));
}

void
lulu_Compiler_emit_return(lulu_Compiler *self)
{
    lulu_Compiler_emit_opcode(self, OP_RETURN);
}

byte3
lulu_Compiler_make_constant(lulu_Compiler *self, const lulu_Value *value)
{
    lulu_VM *vm    = self->vm;
    isize    index = lulu_Chunk_add_constant(vm, current_chunk(self), value);
    if (index > LULU_MAX_CONSTANTS) {
        lulu_Parser_error_consumed(self->parser, "Too many constants in one chunk.");
        return 0;
    }
    return cast(byte3)index;
}

void
lulu_Compiler_emit_constant(lulu_Compiler *self, const lulu_Value *value)
{
    byte3 index = lulu_Compiler_make_constant(self, value);
    emit_instruction(self, lulu_Instruction_make_byte3(OP_CONSTANT, index));
}

void
lulu_Compiler_emit_string(lulu_Compiler *self, const lulu_Token *token)
{
    lulu_Value tmp;
    lulu_Value_set_string(&tmp, lulu_String_new(self->vm, token->start, token->len));
    lulu_Compiler_emit_constant(self, &tmp);
}

void
lulu_Compiler_emit_number(lulu_Compiler *self, lulu_Number n)
{
    lulu_Value tmp;
    lulu_Value_set_number(&tmp, n);
    lulu_Compiler_emit_constant(self, &tmp);
}

static bool
folded_instruction(lulu_Compiler *self, lulu_Instruction inst)
{
    lulu_OpCode  op    = lulu_Instruction_get_opcode(inst);
    lulu_Chunk  *chunk = current_chunk(self);

    // Can't possibly have a previous opcode?
    if (chunk->len <= 0) {
        return false;
    }

    // Poke at the most recently (i.e: previous) written opcode.
    lulu_Instruction *ip = &chunk->code[chunk->len - 1];
    if (lulu_Instruction_get_opcode(*ip) != op) {
        return false;
    }

    // e.g. folded CONCATs always requires 1 less actual intermediate.
    int offset = 0;
    switch (op) {
    case OP_CONCAT: offset = -1;
    case OP_POP:
    case OP_NIL: {
        int prev_arg = cast(int)lulu_Instruction_get_A(*ip);
        int cur_arg  = cast(int)lulu_Instruction_get_A(inst);
        int new_arg  = prev_arg + cur_arg + offset;
        if (0 < new_arg && new_arg <= cast(int)LULU_MAX_BYTE) {
            *ip = lulu_Instruction_make_byte1(op, cast(byte)new_arg);
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
lulu_Compiler_emit_byte1(lulu_Compiler *self, lulu_OpCode op, byte a)
{
    lulu_Instruction inst = lulu_Instruction_make_byte1(op, a);
    if (!folded_instruction(self, inst)) {
        emit_instruction(self, inst);
    }
}

void
lulu_Compiler_emit_byte3(lulu_Compiler *self, lulu_OpCode op, byte3 arg)
{
    emit_instruction(self, lulu_Instruction_make_byte3(op, arg));
}

void
lulu_Compiler_end(lulu_Compiler *self)
{
    lulu_Compiler_emit_return(self);
#ifdef LULU_DEBUG_PRINT
    printf("=== STACK USAGE ===\n"
           "Net: %i\n"
           "\n",
           self->stack_usage);
    lulu_Debug_disasssemble_chunk(current_chunk(self));
#endif
}

void
lulu_Compiler_compile(lulu_Compiler *self, cstring input, lulu_Chunk *chunk)
{
    lulu_Lexer  lexer;
    lulu_Parser parser;

    /**
     * @note 2024-10-30
     *      Could be in 'lulu_Compiler_init()'...
     */
    self->chunk  = chunk;
    self->lexer  = &lexer;
    self->parser = &parser;
    lulu_Parser_init(&parser, self, &lexer);
    lulu_Lexer_init(self->vm, &lexer, chunk->filename, input);

    lulu_Parser_advance_token(&parser);

    lulu_Compiler_begin_scope(self);
    while (!lulu_Parser_match_token(&parser, TOKEN_EOF)) {
        lulu_Parser_declaration(&parser);
    }
    lulu_Compiler_end_scope(self);
    lulu_Compiler_end(self);
}

void
lulu_Compiler_begin_scope(lulu_Compiler *self)
{
    self->scope_depth++;
}

void
lulu_Compiler_end_scope(lulu_Compiler *self)
{
    self->scope_depth--;

    // Remove this scope's local variables.
    while (self->n_locals > 0 && self->locals[self->n_locals -1].depth > self->scope_depth) {
        lulu_Compiler_emit_byte1(self, OP_POP, 1);
        self->n_locals--;
    }
}

static bool
identifiers_equal(const lulu_String *string, const lulu_Token *token)
{
    if (string->len != token->len) {
        return false;
    }
    return memcmp(string->data, token->start, string->len) == 0;
}

void
lulu_Compiler_add_local(lulu_Compiler *self, const lulu_Token *ident)
{
    if (self->n_locals >= cast(int)LULU_MAX_BYTE) {
        lulu_Parser_error_consumed(self->parser, "Too many local variables in function");
        return;
    }

    // Are we shadowing?
    for (int i = self->n_locals - 1; i >= 0; i--) {
        lulu_Local *local = &self->locals[i];
        // Checking an already defined variable of an enclosing scope?
        if (local->depth != -1 && local->depth < self->scope_depth) {
            break;
        }
        if (identifiers_equal(local->name, ident)) {
            lulu_Parser_error_consumed(self->parser, "Shadowing of local variable");
            return;
        }
    }

    lulu_Local *local = &self->locals[self->n_locals++];
    local->name  = lulu_String_new(self->vm, ident->start, ident->len);
    local->depth = UNINITIALIZED_LOCAL;
}

void
lulu_Compiler_initialize_locals(lulu_Compiler *self)
{
    for (int i = self->n_locals - 1; i >= 0; i--) {
        lulu_Local *local = &self->locals[i];
        if (local->depth != UNINITIALIZED_LOCAL) {
            break;
        }
        local->depth = self->scope_depth;
    }
}

int
lulu_Compiler_resolve_local(lulu_Compiler *self, const lulu_Token *ident)
{
    for (int i = self->n_locals - 1; i >= 0; i--) {
        const lulu_Local *local = &self->locals[i];
        if (local->depth != UNINITIALIZED_LOCAL && identifiers_equal(local->name, ident)) {
            return i;
        }
    }
    return UNRESOLVED_LOCAL;
}

isize
lulu_Compiler_new_table(lulu_Compiler *self)
{
    isize index = current_chunk(self)->len;
    lulu_Compiler_emit_opcode(self, OP_NEWTABLE);
    return index;
}

void
lulu_Compiler_adjust_table(lulu_Compiler *self, isize i_code, isize n_fields)
{
    lulu_Instruction *ip = &current_chunk(self)->code[i_code];
    if (n_fields == 0) {
        return;
    }
    *ip = lulu_Instruction_make_byte3(OP_NEWTABLE, n_fields);
}

void
lulu_Compiler_set_table(lulu_Compiler *self, int i_table, int i_key, int n_pop)
{
    emit_instruction(self, lulu_Instruction_make(OP_SETTABLE, i_table, i_key, n_pop));
}
