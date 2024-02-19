#include "common.h"
#include "chunk.h"

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    LuaChunk chunk;
    init_chunk(&chunk);

    int line = 1;
    write_constant(&chunk, make_number(1.2), line);
    write_constant(&chunk, make_number(3.4), line);

    line++;
    write_chunk(&chunk, OP_RETURN, line);
    disassemble_chunk(&chunk, "test chunk");

    deinit_chunk(&chunk);
    return 0;
}
