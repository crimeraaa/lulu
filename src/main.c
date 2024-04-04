#include "conf.h"
#include "chunk.h"
#include "debug.h"

int main(int argc, const char *argv[]) {
    unused2(argc, argv); 
    Chunk chunk;
    init_chunk(&chunk, "test chunk");

    int index = add_constant(&chunk, &make_number(1.2));
    int line = 123;
    write_chunk(&chunk, OP_CONSTANT, line);
    write_chunk(&chunk, index, line);

    write_chunk(&chunk, OP_RETURN, line);
    disassemble_chunk(&chunk);
    free_chunk(&chunk);
    return 0;
}
