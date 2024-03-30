#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "opcodes.h"
#include "vm.h"

void init_compiler(Compiler *self, lua_VM *vm, Lexer *lex) {
    self->lex = lex;
    self->vm  = vm;
    self->freereg = 0;
}

// Later on when function definitions are involved this will get complicated.
static Chunk *current_chunk(Compiler *self) {
    return self->vm->chunk;
}

// Append the given `instruction` to the current compiling chunk.
static void emit_instruction(Compiler *self, Instruction instruction) {
    write_chunk(current_chunk(self), instruction, self->lex->token.line);
}

/**
 * @param results   Number of expected return values for the current compiling
 *                  function.
 */
static void emit_return(Compiler *self, int results) {
    emit_instruction(self, CREATE_ABC(OP_RETURN, 0, results, 0));
}

// Assumes that `int` can indeed more than `MAXARG_Bx`.
static Instruction make_constant(Compiler *self, const TValue *value) {
    int index = add_constant(current_chunk(self), value);
    if (index >= MAXARG_Bx) {
        lexerror_consumed(self->lex, "Too many constants in one chunk.");
        return 0;
    }
    return cast(Instruction, index);
}

// TODO: How to manage emitting to register A here?
static void emit_constant(Compiler *self, const TValue *value) {
    Instruction index = make_constant(self, value);
    emit_instruction(self, CREATE_ABx(OP_CONSTANT, 0, index));
}

// When compiling Lua, 1 return 'value' is always emitted even if it gets
// ignored by a user-specified `return`.
static void end_compiler(Compiler *self) {
    emit_return(self, 1);
#ifdef DEBUG_PRINT_CODE
    disassemble_chunk(current_chunk(self));
#endif
}

// Assumes we just consumed a `TK_NUMBER` token, it is now in `lex->token`.
void number(Compiler *self) {
    Lexer *lex = self->lex;
    const Token *token = &lex->token;
    char *endptr;
    lua_Number value = lua_str2num(token->start, &endptr);

    // This indicates something went wrong in the conversion. We need to throw.
    if (endptr != (token->start + token->len)) {
        lexerror_consumed(lex, "Malformed number");
    }
    emit_constant(self, &makenumber(value));
}

static void expression(Compiler *self) {
    unused(self);
}

bool compile(Compiler *self, const char *input) {
    lua_VM *vm   = self->vm;
    Lexer *lex   = self->lex;
    Chunk *chunk = vm->chunk;
    init_lex(lex, self, chunk->name, input);
    next_token(lex);
    expression(self);
    consume_token(lex, TK_EOF, "Expected end of expression");
    end_compiler(self);
    return true;
}
