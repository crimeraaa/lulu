#ifndef LULU_VIRTUAL_MACHINE_H
#define LULU_VIRTUAL_MACHINE_H

#include "lulu.h"
#include "chunk.h"
#include "object.h"

struct VM {
    TValue stack[MAX_STACK];
    TValue *top;      // Pointer to first free slot in the stack.
    Chunk *chunk;     // Bytecode, constants and such.
    Byte *ip;  // Pointer to next instruction to be executed.
    const char *name; // Filename or `"stdin"` if in REPL.
    Table strings;    // Collection of all interned strings.
    Object *objects;  // Head of linked list to all allocated objects.
    jmp_buf errorjmp; // Used for error-handling (kinda) like C++ exceptions.
};

void init_vm(VM *self, const char *name);
void free_vm(VM *self);
void push_vm(VM *self, const TValue *value);
TValue pop_vm(VM *self);
ErrType interpret(VM *self, const char *input);

#endif /* LULU_VIRTUAL_MACHINE_H */
