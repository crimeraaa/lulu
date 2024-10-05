#ifndef LULU_VM_H
#define LULU_VM_H

#include "chunk.h"
#include "table.h"
#include "builder.h"

#define LULU_VM_STACK_MAX   256

typedef struct lulu_Error_Handler lulu_Error_Handler;

struct lulu_Error_Handler {
    volatile lulu_Status status; // 'volatile' so it never gets optimized out.
    lulu_Jump_Buffer     buffer; // @note 2024-09-22: Unused in C++.
    lulu_Error_Handler  *prev;   // Chain error handlers together.
};

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
    lulu_Value          values[LULU_VM_STACK_MAX];
    lulu_Value         *base; // Points to index 0 of `values`.
    lulu_Value         *top;  // Points to 1 past the most recently written stack slot.
    lulu_Value         *end;  // Points to 1 past the last valid stack slot.

    lulu_Table          strings;  // Hashtable for interned strings.
    lulu_Table          globals;  // Map global variable names to values.
    lulu_Builder        builder;  // Buffer for string literals and concatenations.

    lulu_Allocator      allocator;
    void               *allocator_data;

    lulu_Chunk         *chunk;
    lulu_Instruction   *ip;    // Points to next instruction to be executed.
    lulu_Object        *objects;  // Intrusive linked list of objects.
    lulu_Error_Handler *handlers; // Currently active error handler.
};

void
lulu_VM_init(lulu_VM *self, lulu_Allocator allocator, void *allocator_data);

void
lulu_VM_free(lulu_VM *self);

lulu_Status
lulu_VM_interpret(lulu_VM *self, cstring name, cstring input);

void
lulu_VM_push(lulu_VM *self, const lulu_Value *value);

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

LULU_ATTR_NORETURN
void
lulu_VM_throw_error(lulu_VM *self, lulu_Status status);

LULU_ATTR_NORETURN
void
lulu_VM_comptime_error(lulu_VM *self, cstring file, int line, cstring msg, const char *where, isize len);

LULU_ATTR_NORETURN LULU_ATTR_PRINTF(2, 3)
void
lulu_VM_runtime_error(lulu_VM *self, cstring fmt, ...);

#endif // LULU_VM_H
