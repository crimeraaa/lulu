#ifndef LULU_VIRTUAL_MACHINE_H
#define LULU_VIRTUAL_MACHINE_H

#include "lulu.h"
#include "chunk.h"
#include "object.h"

struct VM {
    Chunk *chunk;     // Bytecode, constants and such.
    Instruction *ip;  // Pointer to next instruction to be executed.
    TValue stack[LULU_MAXSTACK];
    TValue *top;      // Pointer to first free slot in the stack.
    const char *name; // Filename or `"stdin"` if in REPL.
    jmp_buf errorjmp; // Used for error-handling (kinda) like C++ exceptions.
};

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void init_vm(VM *self, const char *name);
void free_vm(VM *self);
void push_vm(VM *self, const TValue *value);
TValue pop_vm(VM *self);
InterpretResult interpret(VM *self, const char *input);

#endif /* LULU_VIRTUAL_MACHINE_H */
