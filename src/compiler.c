/// local
#include "compiler.h"
#include "vm.h"
#include "parser.h"
#include "debug.h"

/// standard
#include <string.h>

void
lulu_Compiler_init(lulu_VM *vm, lulu_Compiler *self)
{
    self->vm     = vm;
    self->chunk  = NULL;
    self->parser = NULL;
    self->lexer  = NULL;

    self->local_count = 0;
    self->scope_depth = 0;
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
emit_instruction(lulu_Compiler *self, lulu_Instruction inst)
{
    lulu_VM *vm   = self->vm;
    int      line = self->parser->consumed.line;
    lulu_Chunk_write(vm, current_chunk(self), inst, line);
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
        int prev_arg = cast(int)lulu_Instruction_get_byte1(*ip);
        int cur_arg  = cast(int)lulu_Instruction_get_byte1(inst);
        int new_arg  = prev_arg + cur_arg + offset;
        if (0 < new_arg && new_arg <= cast(int)LULU_MAX_BYTE) {
            *ip = lulu_Instruction_make_byte1(op, cast(byte)new_arg);
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
    while (self->local_count > 0 && self->locals[self->local_count -1].depth > self->scope_depth) {
        lulu_Compiler_emit_byte1(self, OP_POP, 1);
        self->local_count--;
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
    if (self->local_count >= cast(int)LULU_MAX_BYTE) {
        lulu_Parser_error_consumed(self->parser, "Too many local variables in function");
        return;
    }

    // Are we shadowing?
    for (int i = self->local_count - 1; i >= 0; i--) {
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

    lulu_Local *local = &self->locals[self->local_count++];
    local->name  = lulu_String_new(self->vm, ident->start, ident->len);
    local->depth = UNINITIALIZED_LOCAL;
}

void
lulu_Compiler_initialize_locals(lulu_Compiler *self)
{
    for (int i = self->local_count - 1; i >= 0; i--) {
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
    for (int i = self->local_count - 1; i >= 0; i--) {
        const lulu_Local *local = &self->locals[i];
        if (local->depth != UNINITIALIZED_LOCAL && identifiers_equal(local->name, ident)) {
            return i;
        }
    }
    return UNRESOLVED_LOCAL;
}
