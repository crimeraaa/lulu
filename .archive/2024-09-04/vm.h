#ifndef LULU_VIRTUAL_MACHINE_H
#define LULU_VIRTUAL_MACHINE_H

#include "lulu.h"
#include "chunk.h"
#include "object.h"
#include "memory.h"
#include "zio.h"

// Pointer to a value INSIDE the stack.
typedef Value *StackID;

// To be run by `luluVM_run_protected`.
typedef void (*ProtectedFn)(lulu_VM *vm, void *ctx);

// Defined inside `vm.c`.
typedef struct lulu_Error Error;

struct lulu_VM {
    Value     stack[LULU_MAX_STACK + LULU_STACK_RESERVED];
    Buffer    buffer;    // Used by Lexer and for concatenating.
    Allocator allocator; // Desired allocation function.
    void     *context;   // Context data-pointer for `allocator`.
    StackID   top;       // Pointer to first free slot in the stack.
    StackID   base;      // Pointer to bottom of current stack frame.
    Chunk    *chunk;     // Bytecode, constants and such.
    Byte     *ip;        // Pointer to next instruction to be executed.
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
void luluVM_execute(lulu_VM *vm);

// https://www.lua.org/source/5.1/ldo.c.html#luaD_rawrunprotected
lulu_Status luluVM_run_protected(lulu_VM *vm, ProtectedFn fn, void *ctx);
void luluVM_throw_error(lulu_VM *vm, lulu_Status code);

#endif /* LULU_VIRTUAL_MACHINE_H */
