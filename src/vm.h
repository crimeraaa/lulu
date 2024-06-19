#ifndef LULU_VIRTUAL_MACHINE_H
#define LULU_VIRTUAL_MACHINE_H

#include "lulu.h"
#include "chunk.h"
#include "object.h"
#include "memory.h"

typedef Value *StackID; // Pointer to a Lua stack value.

struct lulu_VM {
    Value       stack[MAX_STACK + STACK_RESERVED];
    Allocator   allocator; // By default, holds a NULL context.
    StackID     top;       // Pointer to first free slot in the stack.
    StackID     base;      // Pointer to bottom of current stack frame.
    Chunk      *chunk;     // Bytecode, constants and such.
    Byte       *ip;        // Pointer to next instruction to be executed.
    const char *name;      // Filename or `"stdin"` if in REPL.
    Table       globals;   // Maps identifiers to Values.
    Table       strings;   // Collection of all interned strings.
    Object     *objects;   // Head of linked list to all allocated objects.
    jmp_buf     errorjmp;  // Used for error-handling (kinda) like C++ exceptions.
};

#define update_top(vm, n)   ((vm)->top += (n))
#define incr_top(vm)        update_top(vm, 1)
#define poke_top(vm, n)     ((vm)->top + (n))
#define poke_base(vm, n)    ((vm)->base + (n))
#define push_back(vm, v)    *(vm)->top = *(v), incr_top(vm)

void luluVM_init(lulu_VM *vm);
void luluVM_free(lulu_VM *vm);
lulu_Status luluVM_execute(lulu_VM *vm);

#endif /* LULU_VIRTUAL_MACHINE_H */
