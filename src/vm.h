#ifndef LUA_VIRTUAL_MACHINE_H
#define LUA_VIRTUAL_MACHINE_H

#include "lua.h"
#include "chunk.h"

struct lua_VM {
    TValue stack[LUA_MAXSTACK];
    TValue *top;     // First free slot in the stack.
    Chunk *chunk;    // Bytecode to be executed and constants list.
    Instruction *ip; // Points to next instruction to be executed.
};

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void init_vm(lua_VM *self);
void free_vm(lua_VM *self);
InterpretResult interpret(lua_VM *self, const char *input);

#endif /* LUA_VIRTUAL_MACHINE_H */
