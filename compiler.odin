#+private
package lulu

import "core:log"

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
    compiler_end(compiler, parser)
    return !parser.panicking
}

compiler_end :: proc(compiler: ^Compiler, parser: ^Parser) {
    compiler_emit_return(compiler, parser)
    when DEBUG_PRINT_CODE {
        if !parser.panicking {
            debug_disasm_chunk(current_chunk(compiler)^)
        }
    }
}

// Analogous to 'compiler.c:emitReturn()' in the book.
// Similar to Lua, all functions have this return even if they have explicit returns.
compiler_emit_return :: proc(compiler: ^Compiler, parser: ^Parser) {
    // Avoid unsigned integer underflow
    reg := cast(u16)compiler.free_reg if compiler.free_reg > 0 else 1
    inst := inst_create_AB(.Return, reg - 1, 1)
    compiler_emit_instruction(compiler, parser, inst)
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
compiler_emit_instruction :: proc(compiler: ^Compiler, parser: ^Parser, inst: Instruction) {
    chunk_append(current_chunk(compiler), inst, parser.consumed.line)
}

@(private="file")
current_chunk :: proc(compiler: ^Compiler) -> (chunk: ^Chunk) {
    return compiler.chunk
}
