#include "common.h"
#include "chunk.h"
#include "vm.h"

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    LuaVM vm;
    LuaChunk chunk;
    init_chunk(&chunk);
    init_vm(&vm);

    int line = 1;
    // Challenge 15.1: Write bytecode for 1 + 2 * 3 - 4 / -5
    write_constant(&chunk, make_luanumber(1), line);
    write_constant(&chunk, make_luanumber(2), line);
    write_constant(&chunk, make_luanumber(3), line);
    write_chunk(&chunk, OP_MUL, line);
    write_constant(&chunk, make_luanumber(4), line);
    write_constant(&chunk, make_luanumber(5), line);
    write_chunk(&chunk, OP_UNM, line);
    write_chunk(&chunk, OP_DIV, line);
    write_chunk(&chunk, OP_SUB, line);
    write_chunk(&chunk, OP_ADD, line);
    
    line++;
    write_chunk(&chunk, OP_RET, line);
    disassemble_chunk(&chunk, "vm.chunk");
    interpret_vm(&vm, &chunk);
    deinit_chunk(&chunk);
    deinit_vm(&vm);
    return 0;
}
