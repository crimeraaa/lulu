#ifndef LUA_VIRTUAL_MACHINE_H
#define LUA_VIRTUAL_MACHINE_H

#include "common.h"
#include "conf.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

struct lua_VM {
    TValue stack[LUA_MAXSTACK]; // Hardcoded limit for simplicity.
    Chunk *chunk;  // Chunk of bytecode and constant values.
    Byte *ip;      // Byte instruction pointer (next instruction) in `chunk`.
    TValue *bp;    // Base pointer to bottom of current stack frame/function.
    TValue *sp;    // Stack pointer to 1 past the lastest written element.
    Table globals; // Interned global variable identifiers, as strings.
    Table strings; // Interned string literals/user-created ones.
    lua_Object *objects; // Head of intrusive linked list of allocated objects.
};

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void init_vm(lua_VM *self);
void free_vm(lua_VM *self);

/**
 * Given a monolithic string of source code...
 */
InterpretResult interpret_vm(lua_VM *self, const char *source);

/**
 * Assigns the top of stack to `value` then increments the VM's stack pointer
 * so that it points to the next available slot in the stack.
 */
void push_vmstack(lua_VM *self, TValue value);

/**
 * Decrements the VM's stack pointer so that it points to the previous slot in
 * the stack and returns the value that was previously there.
 * 
 * Since we only use a stack-allocated array, we don't need to do much cleanup.
 * Moving the stack pointer downwards already indicates the slot is free.
 */
TValue pop_vmstack(lua_VM *self);

#endif /* LUA_VIRTUAL_MACHINE_H */
