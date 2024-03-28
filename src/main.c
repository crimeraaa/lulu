#include "opcodes.h"
#include "chunk.h"
#include "vm.h"
#include "debug.h"

static lua_VM global_vm = {0};

// NOTE: For now, we have to manage registers ourselves! 
// Compile the expression `-(1.2 + 3.4 / 5.6)`.
static void expression_1(lua_VM *vm) {
    int line      = 123; 
    Chunk *chunk  = &(Chunk){0};
    TValue *value = &(TValue){0};
    
    init_chunk(chunk);
    setnumber(value, 1.2);

    // The very first value, if it is a literal, must be stored in a register.
    // This is so we have something on top of the stack to work with.
    int kbx = add_constant(chunk, value);
    
    // R(A=0) := Kst(Bx=0)
    //        |  1.2
    // Stack  := [1.2]
    write_chunk(chunk, CREATE_ABx(OP_CONSTANT, 0, kbx), line);
     
    setnumber(value, 3.4);
    // Use the RKASK macro to convert 0-based contant indexes into 9-bit integer
    // with the 9th bit set to 1 to indicate it's a constant index rather than a
    // register index. So RK(256) => Kst(0), RK(257) => Kst(1), etc.
    int rkc = RKASK(add_constant(chunk, value));
    
    // R(A=0) := R(B=0) + Kst(C=1)
    //        |  1.2 + 3.4
    // STACK  := [4.6]
    write_chunk(chunk, CREATE_ABC(OP_ADD, 0, 0, rkc), line);

    setnumber(value, 5.6); 
    rkc = RKASK(add_constant(chunk, value));

    // R(A=0) := R(B=0) / Kst(C=2) 
    //        |  4.6 / 5.6
    // STACK  := [0.8214]
    write_chunk(chunk, CREATE_ABC(OP_DIV, 0, 0, rkc), line);

    // R(A=0) := -R(B=0)
    //        |  -0.8214
    // STACK  := [-0.8214]
    write_chunk(chunk, CREATE_ABx(OP_UNM, 0, 0), line);
    write_chunk(chunk, CREATE_ABC(OP_RETURN, 0, 1, 0), 111);
    disassemble_chunk(chunk, "-((1.2 + 3.4) / 5.6)");

    interpret(vm, chunk);
    free_chunk(chunk);
}

// Compile the expression `1 + 2 + 3`.
// It is compiled as `(1 + 2) + 3`, where `(1 + 2)` is evaluated first.
static void expression_2(lua_VM *vm) {
    int line      = 234;
    Chunk *chunk  = &(Chunk){0};
    TValue *value = &(TValue){0}; 
    int kindex    = 0;
    
    init_chunk(chunk);
    setnumber(value, 1);
    kindex = add_constant(chunk, value);
    
    // R(A=0) := Kst(B=0)
    //        |  1
    // STACK  := [1]
    write_chunk(chunk, CREATE_ABx(OP_CONSTANT, 0, kindex), line);

    setnumber(value, 2);
    kindex = RKASK(add_constant(chunk, value));
    
    // R(A=0) := RK(B=0) + RK(C=256)
    //        |  R(0)    + Kst(1)
    //        |  1       + 2
    // STACK  := [3]
    write_chunk(chunk, CREATE_ABC(OP_ADD, 0, 0, kindex), line);
    
    setnumber(value, 3);
    kindex = RKASK(add_constant(chunk, value));

    // R(A=0) := RK(B=0) + RK(C=257)
    //        |  R(0)    + Kst(2)
    //        |  3       + 3
    // STACK  := [6]
    write_chunk(chunk, CREATE_ABC(OP_ADD, 0, 0, kindex), line);
    write_chunk(chunk, CREATE_ABC(OP_RETURN, 0, 1, 0), line);

    disassemble_chunk(chunk, "1 + 2 + 3");
    interpret(vm, chunk);
    free_chunk(chunk);
}

// Compile the expression `1 + 2 * 3`. Ensure correct order of operations.
// This one is a bit messy since I'm trying to an approach more like the one in
// the book where we push each constant. We could avoid that by simply using the
// RKASK macro. Lua optimizes out constant arithmetic expressions.
static void expression_3(lua_VM *vm) {
    int line      = 456;
    Chunk *chunk  = &(Chunk){0}; 
    TValue *value = &(TValue){0};
    int kindex    = 0;

    init_chunk(chunk);
    setnumber(value, 1);
    kindex = add_constant(chunk, value);
    write_chunk(chunk, CREATE_ABx(OP_CONSTANT, 0, kindex), line);

    setnumber(value, 2);
    kindex = add_constant(chunk, value);
    write_chunk(chunk, CREATE_ABx(OP_CONSTANT, 1, kindex), line);
    
    setnumber(value, 3);
    kindex = add_constant(chunk, value);
    write_chunk(chunk, CREATE_ABx(OP_CONSTANT, 2, kindex), line);
    
    // [ R(0) := 1 ][ R(1) := 2 ][ R(2) := 3 ]
    write_chunk(chunk, CREATE_ABC(OP_MUL, 1, 1, 2), line);
    
    // [ R(0) := 1 ][ R(1) := 6 ]
    write_chunk(chunk, CREATE_ABC(OP_ADD, 0, 0, 1), line);

    // [ R(0) := 7 ]
    write_chunk(chunk, CREATE_ABC(OP_RETURN, 0, 1, 0), line); 

    disassemble_chunk(chunk, "1 + 2 * 3");
    interpret(vm, chunk);
    free_chunk(chunk);
}

// Compile the expression `3 - 2 - 1`.
static void expression_4(lua_VM *vm) {
    int line = 567;
    Chunk *chunk = &(Chunk){0};
    TValue *value = &(TValue){0}; 
    int kindex = 0;

    init_chunk(chunk); 
    setnumber(value, 3);
    kindex = add_constant(chunk, value);
    
    // R(A = 0) := Kst(0)
    //          |  3
    // STACK    := [ 3 ]
    write_chunk(chunk, CREATE_ABx(OP_CONSTANT, 0, kindex), line);

    setnumber(value, 2);
    kindex = RKASK(add_constant(chunk, value));
    
    // R(A = 0) := RK(B = 0) - RK(C = 257)
    //          |  R(0) - Kst(1)
    //          |  3 - 2
    // STACK    := [ 1 ]
    write_chunk(chunk, CREATE_ABC(OP_SUB, 0, 0, kindex), line);

    setnumber(value, 1);
    kindex = RKASK(add_constant(chunk, value));

    // R(A = 0) := RK(B = 0) - RK(C = 258)
    //          |  R(0) - Kst(2)
    //          |  1 - 1
    // STACK    := [ 0 ]
    write_chunk(chunk, CREATE_ABC(OP_SUB, 0, 0, kindex), line);

    // Return 1 value: R(A = 0) a.k.a 0.
    write_chunk(chunk, CREATE_ABC(OP_RETURN, 0, 1, 0), line);
    disassemble_chunk(chunk, "3 - 2 - 1");
    interpret(vm, chunk);
    free_chunk(chunk);
}

// Compile the expression `1 + 2 * 3 - 4 / -5`. Note the order of operations.
static void expression_5(lua_VM *vm) {
    int line = 42;
    Chunk *chunk = &(Chunk){0};
    int kindex = 0;
    
    init_chunk(chunk);

    // R(A = 0) := Kst(0)
    //          |  1
    // STACK    := [ 1 ]
    kindex = add_constant(chunk, &makenumber(1));
    write_chunk(chunk, CREATE_ABx(OP_CONSTANT, 0, kindex), line);

    // R(A = 1) := Kst(1)
    //          |  2
    // STACK    := [ 1 ][ 2 ]
    kindex = add_constant(chunk, &makenumber(2));
    write_chunk(chunk, CREATE_ABx(OP_CONSTANT, 1, kindex), line);    
    
    // R(A = 2) := Kst(2)
    //          |  3
    // STACK    := [ 1 ][ 2 ][ 3 ]
    kindex = add_constant(chunk, &makenumber(3));
    write_chunk(chunk, CREATE_ABx(OP_CONSTANT, 2, kindex), line);
    
    // R(A = 1) := RK(B = 1) * RK(C = 2)
    //          | R(1) * R(2)
    //          | 2 * 3
    // STACK    := [ 1 ][ 6 ][ 3 ]  ; Currently we can't pop '3' off.
    write_chunk(chunk, CREATE_ABC(OP_MUL, 1, 1, 2), line);
    
    // R(A = 0) := RK(B = 0) + RK(C = 1)
    //          |  R(0) + R(1)
    //          | 1 + 6
    // STACK    := [ 7 ][ 6 ][ 3 ]  ; Haven't popped '6' and '3' off yet.
    write_chunk(chunk, CREATE_ABC(OP_ADD, 0, 0, 1), line);

    // R(A = 1) := Kst(3)
    //          |  4
    // STACK    := [ 7 ][ 4 ][ 3 ]  ; '3' is a leftover from before.
    kindex = add_constant(chunk, &makenumber(4));
    write_chunk(chunk, CREATE_ABx(OP_CONSTANT, 1, kindex), line);

    // R(A = 2) := Kst(4)
    //          |  5
    // STACK    := [ 7 ][ 4 ][ 5 ]  ; '3' has been overwritten by '5'.
    kindex = add_constant(chunk, &makenumber(5));
    write_chunk(chunk, CREATE_ABx(OP_CONSTANT, 2, kindex), line);
    
    // R(A = 2) := -R(B - 2)
    //          := -5
    // STACK    := [ 7 ][ 4 ][ -5 ]
    write_chunk(chunk, CREATE_ABC(OP_UNM, 2, 2, 0), line);
    
    // R(A = 1) := RK(B = 1) / RK(C = 2)
    //          |  R(1) / R(2)
    //          |  4 / -5
    // STACK    := [ 7 ][ -0.8 ]
    write_chunk(chunk, CREATE_ABC(OP_DIV, 1, 1, 2), line);
    
    // R(A = 0) := RK(B = 0) - RK(C = 1)
    //          |  R(0) - R(1)
    //          |  7 - -0.8
    // STACK    := [ 7.8 ][ -0.8 ] ; '-0.8' has not been popped.
    write_chunk(chunk, CREATE_ABC(OP_SUB, 0, 0, 1), line);

    write_chunk(chunk, CREATE_ABC(OP_RETURN, 0, 1, 0), line);
    disassemble_chunk(chunk, "1 + 2 * 3 - 4 / -5");
    interpret(vm, chunk);
    free_chunk(chunk);
}

int main(int argc, const char *argv[]) {
    unused(argc);
    unused(argv);

    lua_VM *vm = &global_vm;
    init_vm(vm);
    expression_1(vm); printf("\n");
    expression_2(vm); printf("\n");
    expression_3(vm); printf("\n");
    expression_4(vm); printf("\n");
    expression_5(vm);
    free_vm(vm);
    return 0;
}
