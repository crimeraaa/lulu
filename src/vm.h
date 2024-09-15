#ifndef LULU_VM_H
#define LULU_VM_H

#include "chunk.h"
#include "table.h"

/**
 * @todo 2024-09-06
 *      Change to be configurable. In C++ for example setjmp is a TERRIBLE idea!
 */
#include <setjmp.h>
#include <stdnoreturn.h>

#define LULU_VM_STACK_MAX   256

typedef struct {
    lulu_Value  values[LULU_VM_STACK_MAX];
    lulu_Value *base;  // Points to index 0 of `values`.
    lulu_Value *top;   // Points to 1 past the most recently written stack slot.
    lulu_Value *end;   // Points to 1 past the last valid stack slot.
} lulu_Stack;

typedef struct lulu_Handler {
    volatile lulu_Status status;
    jmp_buf              jump;
    struct lulu_Handler *prev;
} lulu_Handler;

/**
 * @brief
 *      The callback function to be run within `lulu_VM_run_protected()`.
 *
 * @link
 *      https://www.lua.org/source/5.1/ldo.h.html#Pfunc
 */
typedef void
(*lulu_ProtectedFn)(lulu_VM *vm, void *userdata);

struct lulu_VM {
    lulu_Stack        stack;
    lulu_Table        strings;  // Interned strings.
    lulu_Allocator    allocator;
    void             *allocator_data;
    lulu_Chunk       *chunk;
    lulu_Instruction *ip;       // Points to next instruction to be executed.
    lulu_Object      *objects;  // Intrusive linked list of objects.
    lulu_Handler     *handlers; // Currently active error handler.
};

void
lulu_VM_init(lulu_VM *self, lulu_Allocator allocator, void *allocator_data);

void
lulu_VM_free(lulu_VM *self);

lulu_Status
lulu_VM_interpret(lulu_VM *self, cstring name, cstring input);

void
lulu_VM_push(lulu_VM *self, const lulu_Value *value);

lulu_Value
lulu_VM_pop(lulu_VM *self);

/**
 * @brief
 *      Internally, creates a new error handler to wrap the call to `fn`. This
 *      makes it so that thrown errors are recoverable.
 *
 * @link
 *      https://www.lua.org/source/5.1/ldo.c.html#luaD_pcall
 */
lulu_Status
lulu_VM_run_protected(lulu_VM *self, lulu_ProtectedFn fn, void *userdata);

noreturn void
lulu_VM_throw_error(lulu_VM *self, lulu_Status status);

noreturn void
lulu_VM_comptime_error(lulu_VM *self, cstring file, int line, cstring msg, String where);

__attribute__((__format__ (__printf__, 2, 3)))
noreturn void
lulu_VM_runtime_error(lulu_VM *self, cstring fmt, ...);

#endif // LULU_VM_H
