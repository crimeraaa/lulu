#include "common.h"
#include "opcodes.h"
#include "chunk.h"

int main(int argc, const char *argv[]) {
    Chunk _chunk;
    Chunk *chunk = &_chunk;
    init_chunk(chunk);
    write_chunk(chunk, OP_RETURN);
    disassemble_chunk(chunk, "test chunk");
    free_chunk(chunk);
    return 0;
}
