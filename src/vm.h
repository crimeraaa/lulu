#ifndef LULU_VIRTUAL_MACHINE_H
#define LULU_VIRTUAL_MACHINE_H

#include "lulu.h"
#include "chunk.h"
#include "object.h"
#include "memory.h"

// Forward declared in `lulu.h`.
struct VM {
    Value       stack[MAX_STACK];
    Alloc       alloc;    // Will hold the VM itself as context.
    Value      *top;      // Pointer to first free slot in the stack.
    Value      *base;     // Pointer to bottom of current stack frame.
    Chunk      *chunk;    // Bytecode, constants and such.
    Byte       *ip;       // Pointer to next instruction to be executed.
    const char *name;     // Filename or `"stdin"` if in REPL.
    Table       globals;  // Maps identifiers to Values.
    Table       strings;  // Collection of all interned strings.
    Object     *objects;  // Head of linked list to all allocated objects.
    jmp_buf     errorjmp; // Used for error-handling (kinda) like C++ exceptions.
};

#define popn(vm, n)         ((vm)->top -= (n))
#define pop_back(vm)        (popn(vm, 1))
#define update_top(vm, n)   ((vm)->top += (n))
#define incr_top(vm)        update_top(vm, 1)

void init_vm(VM *self, const char *name);
void free_vm(VM *self);
ErrType interpret(VM *self, const char *input);
void runtime_error(VM *self, const char *act, const char *type);

#endif /* LULU_VIRTUAL_MACHINE_H */
