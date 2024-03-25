#include "opcodes.h"
#include "chunk.h"

int main(int argc, const char *argv[]) {
    Chunk *chunk = &(Chunk){0};
    init_chunk(chunk);

    TValue *n = &(TValue){0};
    setnumber(n, 1.2);
    Instruction instruction = CREATE_ABx(OP_CONSTANT, 1, add_constant(chunk, n));
    write_chunk(chunk, instruction);

    write_chunk(chunk, OP_RETURN);
    disassemble_chunk(chunk, "test chunk");
    free_chunk(chunk);
    return 0;
}
