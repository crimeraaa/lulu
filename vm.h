#ifndef LUA_VIRTUAL_MACHINE_H
#define LUA_VIRTUAL_MACHINE_H

#include "common.h"
#include "chunk.h"
#include "object.h"
#include "value.h"

/** 
 * III:15.2.1: The VM's Stack
 * 
 * For now this is a reasonable default to make, as we don't do heap allocations. 
 * However, in the real world, it's fair to assume that there are projects that
 * end up with stack sizes *greater* than 256. So make your call!
 */
#define LUA_STACK_MAX   (UINT8_MAX + 1)

struct LuaVM {
    Chunk *chunk;
    uint8_t *ip; // Byte instruction pointer into `chunk`.
    TValue stack[LUA_STACK_MAX]; // Hardcoded limit for simplicity.
    TValue *sp; // Stack pointer to 1 past the last element.
    lua_Object *objects; // Head of linked list of allocated objects.
};

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void init_vm(LuaVM *self);
void deinit_vm(LuaVM *self);

/**
 * Given a monolithic string of source code...
 */
InterpretResult interpret_vm(LuaVM *self, const char *source);

/**
 * Assigns the top of stack to `value` then increments the VM's stack pointer
 * so that it points to the next available slot in the stack.
 */
void push_vmstack(LuaVM *self, TValue value);

/**
 * Decrements the VM's stack pointer so that it points to the previous slot in
 * the stack and returns the value that was previously there.
 * 
 * Since we only use a stack-allocated array, we don't need to do much cleanup.
 * Moving the stack pointer downwards already indicates the slot is free.
 */
TValue pop_vmstack(LuaVM *self);

#endif /* LUA_VIRTUAL_MACHINE_H */
