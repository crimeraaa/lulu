#ifndef LULU_VM_H
#define LULU_VM_H

#include "lulu.h"
#include "memory.h"
#include "chunk.h"

#define LULU_VM_STACK_MAX   256

struct lulu_VM {
    lulu_Allocator allocator;
    void *         allocator_data;

    lulu_Chunk *   chunk;
    byte *         ip; // Points to next instruction to be executed.
    lulu_Value     stack[LULU_VM_STACK_MAX];
    lulu_Value *   stack_top;
};

typedef enum {
    LULU_OK,
    LULU_ERR_COMPTIME,
    LULU_ERR_RUNTIME,
} lulu_Status;

void lulu_VM_init(lulu_VM *self, lulu_Allocator allocator, void *allocator_data);
void lulu_VM_free(lulu_VM *self);
lulu_Status lulu_VM_interpret(lulu_VM *self, lulu_Chunk *chunk);

void lulu_VM_push(lulu_VM *self, const lulu_Value *value);
lulu_Value lulu_VM_pop(lulu_VM *self);

#endif // LULU_VM_H
