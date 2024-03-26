#include "opcodes.h"
#include "chunk.h"

int main(int argc, const char *argv[]) {
    Chunk *chunk = &(Chunk){0};
    init_chunk(chunk);

    TValue *n = &(TValue){0};
    setnumber(n, 1.2);
    write_chunk(chunk, CREATE_ABx(OP_CONSTANT, 0, add_constant(chunk, n)), 111);

    write_chunk(chunk, OP_RETURN, 111);
    disassemble_chunk(chunk, "test chunk");
    free_chunk(chunk);
    return 0;
}
