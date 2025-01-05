#+private
package lulu

import "core:fmt"

MAX_CONSTANTS :: MAX_uBC

Compiler :: struct {
    parent: ^Compiler,  // Enclosing state.
    parser: ^Parser,    // All nested compilers share the same parser.
    chunk:  ^Chunk,     // Compilers do not own their chunks. They merely fill them.
    free_reg: int,      // Index of the first free register.
}

compiler_compile :: proc(chunk: ^Chunk, input: string) -> bool {
    parser   := &Parser{lexer = lexer_create(input, chunk.source)}
    compiler := &Compiler{parser = parser, chunk = chunk}
    parser_advance(parser)
    parser_parse_expression(parser, compiler, &Expr{})
    parser_consume(parser, .Eof)
    return !parser.panicking
}

compiler_end :: proc(compiler: ^Compiler) {
    compiler_emit_return(compiler)
}

// Analogous to 'compiler.c:emitReturn()' in the book.
// Similar to Lua, all functions have this return even if they have explicit returns.
compiler_emit_return :: proc(compiler: ^Compiler) {
    compiler_emit_instruction(compiler, inst_create_AB(.Return, 0, 1))
}

// compiler_emit_constant :: proc(compiler: ^Compiler, constant: Value) {
// }

// Analogous to 'compiler.c:makeConstant()' in the book.
compiler_add_constant :: proc(compiler: ^Compiler, constant: Value) -> (index: u32) {
    index = chunk_add_constant(current_chunk(compiler), constant)
    if index >= MAX_CONSTANTS {
        parser_error_consumed(compiler.parser, "Function uses too many constants")
        return 0
    }
    return index
}

// Analogous to 'compiler.c:emitByte()' and 'compiler.c:emitBytes()' in the book.
compiler_emit_instruction :: proc(compiler: ^Compiler, inst: Instruction) {
    chunk_append(current_chunk(compiler), inst, compiler.parser.consumed.line)
}

@(private="file")
current_chunk :: proc(compiler: ^Compiler) -> (chunk: ^Chunk) {
    return compiler.chunk
}
