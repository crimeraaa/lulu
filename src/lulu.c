#include "lulu.h"
#include "chunk.h"
#include "debug.h"
#include "limits.h"
#include "vm.h"

// Compile: `-((1.2 + 3.4) / 5.6)`
static void run1(VM *vm) {
    Chunk chunk;
    init_chunk(&chunk, "test chunk");

    int index = add_constant(&chunk, &make_number(1.2));
    int line = 123;
    write_chunk(&chunk, create_iBx(OP_CONSTANT, index), line);

    index = add_constant(&chunk, &make_number(3.4));
    write_chunk(&chunk, create_iBx(OP_CONSTANT, index), line);
    write_chunk(&chunk, create_iNone(OP_ADD), line);
    
    index = add_constant(&chunk, &make_number(5.6));
    write_chunk(&chunk, create_iBx(OP_CONSTANT, index), line);
    write_chunk(&chunk, create_iNone(OP_DIV), line);
    write_chunk(&chunk, create_iNone(OP_UNM), line);
    write_chunk(&chunk, create_iNone(OP_RETURN), line);

    disassemble_chunk(&chunk);
    interpret(vm, &chunk);

    free_chunk(&chunk);
}

// Compile: `1 + 2 * 3`
static void challenge_1A(VM *vm) {
    Chunk chunk;
    int index;
    int line = 456; 
    init_chunk(&chunk, "1 + 2 * 3");

    index = add_constant(&chunk, &make_number(1));
    write_chunk(&chunk, create_iBx(OP_CONSTANT, index), line);
    
    index = add_constant(&chunk, &make_number(2));
    write_chunk(&chunk, create_iBx(OP_CONSTANT, index), line);
    
    index = add_constant(&chunk, &make_number(3));
    write_chunk(&chunk, create_iBx(OP_CONSTANT, index), line);
    write_chunk(&chunk, create_iNone(OP_MUL), line);
    write_chunk(&chunk, create_iNone(OP_ADD), line);
    write_chunk(&chunk, create_iNone(OP_RETURN), line);
    
    disassemble_chunk(&chunk);
    interpret(vm, &chunk);
    free_chunk(&chunk);
}

int main(int argc, const char *argv[]) {
    unused2(argc, argv); 
    VM vm;
    init_vm(&vm);
    run1(&vm);
    challenge_1A(&vm);
    free_vm(&vm);
    return 0;
}
