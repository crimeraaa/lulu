#ifndef LULU_VIRTUAL_MACHINE_H
#define LULU_VIRTUAL_MACHINE_H

#include "lulu.h"
#include "chunk.h"
#include "object.h"
#include "memory.h"

typedef Value *StackID; // Pointer to a Lua stack value.
typedef void (*ProtectedFn)(lulu_VM *vm, void *ctx);

typedef struct lulu_Error Error;

struct lulu_Error {
    struct lulu_Error   *prev;
    jmp_buf              buffer;
    volatile lulu_Status status; // Volatile as it can be changed outside caller.
};

struct lulu_VM {
    Value     stack[LULU_MAX_STACK + LULU_STACK_RESERVED];
    Allocator allocator; // Desired allocation function.
    void     *context;   // Context data-pointer for `allocator`.
    StackID   top;       // Pointer to first free slot in the stack.
    StackID   base;      // Pointer to bottom of current stack frame.
    Chunk    *chunk;     // Bytecode, constants and such.
    Byte     *ip;        // Pointer to next instruction to be executed.
    String   *name;      // Filename or `"stdin"` if in REPL.
    Table     globals;   // Maps identifiers to Values.
    Table     strings;   // Collection of all interned strings.
    Object   *objects;   // Head of linked list to all allocated objects.
    Error    *errors;    // Linked list of error handlers.
};

#define update_top(vm, n)   ((vm)->top += (n))
#define incr_top(vm)        update_top(vm, 1)
#define poke_top(vm, n)     ((vm)->top + (n))
#define poke_base(vm, n)    ((vm)->base + (n))
#define push_back(vm, v)    *(vm)->top = *(v), incr_top(vm)

// May return `false` if initial allocations failed!
bool luluVM_init(lulu_VM *vm, lulu_Allocator fn, void *ctx);
void luluVM_free(lulu_VM *vm);
lulu_Status luluVM_execute(lulu_VM *vm);

// https://www.lua.org/source/5.1/ldo.c.html#luaD_rawrunprotected
lulu_Status luluVM_run_protected(lulu_VM *vm, ProtectedFn fn, void *ctx);

#endif /* LULU_VIRTUAL_MACHINE_H */
