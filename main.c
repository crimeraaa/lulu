#include "common.h"
#include "chunk.h"

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    LuaChunk chunk;
    init_chunk(&chunk);
    write_chunk(&chunk, OP_RETURN);
    disassemble_chunk(&chunk, "test chunk");
    deinit_chunk(&chunk);
    return 0;
}
