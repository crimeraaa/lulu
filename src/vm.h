#ifndef LULU_VM_H
#define LULU_VM_H

#include "chunk.h"
#include "table.h"
#include "builder.h"

#define LULU_VM_STACK_MAX   256

typedef struct Error_Handler Error_Handler;
struct Error_Handler {
    volatile lulu_Status status; // 'volatile' so it never gets optimized out.
    lulu_Jump_Buffer     buffer; // @note 2024-09-22: Unused in C++.
    Error_Handler       *prev;   // Chain error handlers together.
};

/**
 * @brief
 *      The callback function to be run within `vm_run_protected()`.
 *
 * @link
 *      https://www.lua.org/source/5.1/ldo.h.html#Pfunc
 */
typedef void
(*Protected_Fn)(lulu_VM *vm, void *userdata);

struct lulu_VM {
    Value  values[LULU_VM_STACK_MAX];
    Value *base; // Points to index 0 of `values`.
    Value *top;  // Points to 1 past the most recently written stack slot.
    Value *end;  // Points to 1 past the last valid stack slot.

    Table strings;  // Hashtable for interned strings.
    Table globals;  // Map global variable names to values.
    Builder builder;  // Buffer for string literals and concatenations.

    lulu_Allocator allocator;
    void *allocator_data;

    Chunk         *chunk;
    Instruction   *ip;    // Points to next instruction to be executed.
    Object        *objects;  // Intrusive linked list of objects.
    Error_Handler *handlers; // Currently active error handler.
};

bool
vm_init(lulu_VM *self, lulu_Allocator allocator, void *allocator_data);

void
vm_free(lulu_VM *self);

lulu_Status
vm_interpret(lulu_VM *self, const char *input, isize len, cstring chunk_name);

void
vm_push(lulu_VM *self, const Value *value);

void
vm_concat(lulu_VM *vm, int count);

/**
 * @brief
 *      Internally, creates a new error handler to wrap the call to `fn`. This
 *      makes it so that thrown errors are recoverable.
 *
 * @link
 *      https://www.lua.org/source/5.1/ldo.c.html#luaD_pcall
 */
lulu_Status
vm_run_protected(lulu_VM *self, Protected_Fn fn, void *userdata);

LULU_ATTR_NORETURN
void
vm_throw_error(lulu_VM *self, lulu_Status status);

LULU_ATTR_NORETURN
void
vm_comptime_error(lulu_VM *self, cstring file, int line, cstring msg, const char *where, int len);

LULU_ATTR_NORETURN LULU_ATTR_PRINTF(2, 3)
void
vm_runtime_error(lulu_VM *self, cstring fmt, ...);

#endif // LULU_VM_H
