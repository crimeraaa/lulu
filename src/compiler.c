#include "compiler.h"
#include "vm.h"
#include "parser.h"
#include "debug.h"

void
lulu_Compiler_init(lulu_VM *vm, lulu_Compiler *self)
{
    self->vm    = vm;
    self->chunk = NULL;
    self->prev_opcode = OP_RETURN; // Can't optimize RETURN no matter what!
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
emit_instruction(lulu_Compiler *self, lulu_Parser *parser, lulu_Instruction inst)
{
    lulu_VM *vm   = self->vm;
    int      line = parser->consumed.line;
    lulu_Chunk_write(vm, current_chunk(self), inst, line);
    self->prev_opcode = lulu_Instruction_get_opcode(inst);
}

void
lulu_Compiler_emit_opcode(lulu_Compiler *self, lulu_Parser *parser, lulu_OpCode op)
{
    emit_instruction(self, parser, lulu_Instruction_none(op));
}

void
lulu_Compiler_emit_return(lulu_Compiler *self, lulu_Parser *parser)
{
    lulu_Compiler_emit_opcode(self, parser, OP_RETURN);
}

static byte3
make_constant(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser, const lulu_Value *value)
{
    lulu_VM *vm    = self->vm;
    isize    index = lulu_Chunk_add_constant(vm, current_chunk(self), value);
    if (index > LULU_MAX_CONSTANTS) {
        lulu_Parse_error_consumed(lexer, parser, "Too many constants in one chunk.");
        return 0;
    }
    return cast(byte3)index;
}

void
lulu_Compiler_emit_constant(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser, const lulu_Value *value)
{
    byte3 index = make_constant(self, lexer, parser, value);
    emit_instruction(self, parser, lulu_Instruction_byte3(OP_CONSTANT, index));
}

static bool
folded_instruction(lulu_Compiler *self, lulu_Instruction inst)
{
    lulu_OpCode op = lulu_Instruction_get_opcode(inst);
    if (self->prev_opcode != op) {
        return false;
    }

    lulu_Chunk       *chunk = current_chunk(self);
    lulu_Instruction *ip    = &chunk->code[chunk->len - 1];
    
    // e.g. folded CONCATs always requires 1 less actual intermediate.
    int offset = 0;
    switch (op) {
    case OP_CONCAT: offset = -1;
    case OP_NIL: {
        int old_arg  = cast(int)lulu_Instruction_get_byte1(*ip);
        int new_arg  = cast(int)lulu_Instruction_get_byte1(inst);
        int adjusted = old_arg + new_arg + offset;
        if (0 < adjusted && adjusted < cast(int)MAX_BYTE) {
            *ip = lulu_Instruction_byte1(op, cast(byte)adjusted);
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
lulu_Compiler_emit_byte1(lulu_Compiler *self, lulu_Parser *parser, lulu_OpCode op, byte a)
{
    lulu_Instruction inst = lulu_Instruction_byte1(op, a);
    if (!folded_instruction(self, inst)) {
        emit_instruction(self, parser, inst);
    }
}

void
lulu_Compiler_end(lulu_Compiler *self, lulu_Parser *parser)
{
    lulu_Compiler_emit_return(self, parser);
#ifdef LULU_DEBUG_PRINT
    lulu_Debug_disasssemble_chunk(current_chunk(self));
#endif
}

void
lulu_Compiler_compile(lulu_Compiler *self, cstring input, lulu_Chunk *chunk)
{
    lulu_Lexer  lexer;
    lulu_Parser parser;
    self->chunk = chunk;
    lulu_Lexer_init(self->vm, &lexer, chunk->filename, input);
    lulu_Parse_advance_token(&lexer, &parser);
    lulu_Parse_expression(self, &lexer, &parser);
    lulu_Parse_consume_token(&lexer, &parser, TOKEN_EOF, "Expected end of expression");
    lulu_Compiler_end(self, &parser);
}
