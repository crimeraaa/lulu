#include "compiler.h"
#include "vm.h"
#include "parser.h"

#ifdef LULU_DEBUG_PRINT
#include "debug.h"
#endif

void lulu_Compiler_init(lulu_VM *vm, lulu_Compiler *self, lulu_Lexer *lexer)
{
    self->vm    = vm;
    self->lexer = lexer;
}

/**
 * @note 2024-09-07
 *      This will get more complicated later on, hence we abstract it out now.
 */
static lulu_Chunk *current_chunk(lulu_Compiler *self)
{
    return self->chunk;
}

void lulu_Compiler_emit_byte(lulu_Compiler *self, lulu_Parser *parser, byte inst)
{
    lulu_Chunk_write(self->vm, current_chunk(self), inst, parser->consumed.line);
}

void lulu_Compiler_emit_bytes(lulu_Compiler *self, lulu_Parser *parser, byte inst1, byte inst2)
{
    lulu_Compiler_emit_byte(self, parser, inst1);
    lulu_Compiler_emit_byte(self, parser, inst2);
}

void lulu_Compiler_emit_byte3(lulu_Compiler *self, lulu_Parser *parser, byte3 inst)
{
    lulu_Chunk_write_byte3(self->vm, current_chunk(self), inst, parser->consumed.line);
}

void lulu_Compiler_emit_return(lulu_Compiler *self, lulu_Parser *parser)
{
    lulu_Compiler_emit_byte(self, parser, OP_RETURN);
}

static isize make_constant(lulu_Compiler *self, lulu_Parser *parser, const lulu_Value *value)
{
    isize index = lulu_Chunk_add_constant(self->vm, current_chunk(self), value);
    if (index > LULU_MAX_CONSTANTS) {
        lulu_Parse_error_consumed(self->vm, parser, "Too many constants in one chunk.");
        return 0;
    }
    return index;
}

void lulu_Compiler_emit_constant(lulu_Compiler *self, lulu_Parser *parser, const lulu_Value *value)
{
    isize index = make_constant(self, parser, value);
    lulu_Compiler_emit_byte(self, parser, OP_CONSTANT);
    lulu_Compiler_emit_byte3(self, parser, cast(byte3)index);
}

void lulu_Compiler_end(lulu_Compiler *self, lulu_Parser *parser)
{
    lulu_Compiler_emit_return(self, parser);
#ifdef LULU_DEBUG_PRINT
    lulu_Debug_disasssemble_chunk(current_chunk(self), "code");
#endif
}

void lulu_Compiler_compile(lulu_Compiler *self, cstring input, lulu_Chunk *chunk)
{
    lulu_Lexer *lexer  = self->lexer;
    lulu_Parser parser = {0};
    self->chunk = chunk;
    lulu_Lexer_init(self->vm, self->lexer, input);
    lulu_Parse_advance_token(lexer, &parser);
    lulu_Parse_expression(self, lexer, &parser);
    lulu_Parse_consume_token(lexer, &parser, TOKEN_EOF, "Expected end of expression");
    lulu_Compiler_end(self, &parser);
}
