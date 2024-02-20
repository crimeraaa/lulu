#include "common.h"
#include "chunk.h"
#include "vm.h"

/* Challenge 15.1: Write bytecode for 1 + 2 * 3 - 4 / -5 */
static void test_chunk1A(LuaVM *vm) {
    int line = 11;
    LuaChunk *chunk = &(LuaChunk){0};
    init_chunk(chunk);
    
    write_constant(chunk, make_luanumber(1), line);
    write_constant(chunk, make_luanumber(2), line);
    write_constant(chunk, make_luanumber(3), line);
    write_chunk(chunk, OP_MUL, line);
    write_constant(chunk, make_luanumber(4), line);
    write_constant(chunk, make_luanumber(5), line);
    write_chunk(chunk, OP_UNM, line);
    write_chunk(chunk, OP_DIV, line);
    write_chunk(chunk, OP_SUB, line);
    write_chunk(chunk, OP_ADD, line);
    
    line++;
    write_chunk(chunk, OP_RET, line);
    
    disassemble_chunk(chunk, __func__);
    interpret_vm(vm, chunk);
    deinit_chunk(chunk);
}

/* Testing out my modulo and exponentiation operators. */
static void test_chunk1B(LuaVM *vm) {
    int line = 42;
    LuaChunk *chunk = &(LuaChunk){0};
    init_chunk(chunk);
    
    write_constant(chunk, make_luanumber(5.0), line);
    write_constant(chunk, make_luanumber(4.0), line);
    write_chunk(chunk, OP_POW, line);
    write_constant(chunk, make_luanumber(32.0), line);
    write_chunk(chunk, OP_MOD, line);

    line++;
    write_chunk(chunk, OP_RET, line);

    disassemble_chunk(chunk, __func__);
    interpret_vm(vm, chunk);
    deinit_chunk(chunk);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    LuaVM *vm = &(LuaVM){0}; // C99 compound literals are really handy sometimes
    init_vm(vm);
    test_chunk1A(vm);
    test_chunk1B(vm);
    deinit_vm(vm);
    return 0;
}
