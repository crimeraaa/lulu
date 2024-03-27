#include "opcodes.h"
#include "chunk.h"
#include "vm.h"
#include "debug.h"

static lua_VM g_vm    = {0};
static Chunk  g_chunk = {0};
static TValue g_value = {0};

int main(int argc, const char *argv[]) {
    unused(argc);
    unused(argv);

    lua_VM *vm    = &g_vm;
    Chunk *chunk  = &g_chunk;
    TValue *value = &g_value;
    setnumber(value, 1.2);

    init_vm(vm);
    init_chunk(chunk);

    // NOTE: For now, we have to manage registers ourselves! 
    // R(A=0) = Kst(Bx=0) ; stack[0] = 1.2
    write_chunk(chunk, CREATE_ABx(OP_CONSTANT, 0, add_constant(chunk, value)), 111);
    
    // R(A=0) = -R(B=0)   ; stack[0] = -stack[0]
    write_chunk(chunk, CREATE_ABC(OP_UNM, 0, 0, 0), 111);

    write_chunk(chunk, OP_RETURN, 111);
    disassemble_chunk(chunk, "test chunk");
    interpret(vm, chunk);

    free_chunk(chunk);
    free_vm(vm);
    return 0;
}
