#ifndef LULU_VM_H
#define LULU_VM_H

#include "lulu.h"
#include "memory.h"
#include "chunk.h"

#define LULU_VM_STACK_MAX   256

typedef enum {
    LULU_OK,
    LULU_ERR_COMPTIME,
    LULU_ERR_RUNTIME,
} lulu_Status;

typedef struct {
    lulu_Value  values[LULU_VM_STACK_MAX];
    lulu_Value *base;  // Points to index 0 of `values`.
    lulu_Value *top;   // Points to 1 past the most recently written stack slot.
    const lulu_Value *end; // Points to 1 past the last valid stack slot.
} lulu_VM_Stack;

struct lulu_VM {
    lulu_VM_Stack  stack;
    lulu_Allocator allocator;
    void          *allocator_data;
    lulu_Chunk    *chunk;
    byte          *ip; // Points to next instruction in `chunk` to be executed.
};

void lulu_VM_init(lulu_VM *self, lulu_Allocator allocator, void *allocator_data);
void lulu_VM_free(lulu_VM *self);
lulu_Status lulu_VM_interpret(lulu_VM *self, cstring input);

void lulu_VM_push(lulu_VM *self, const lulu_Value *value);
lulu_Value lulu_VM_pop(lulu_VM *self);

#endif // LULU_VM_H
