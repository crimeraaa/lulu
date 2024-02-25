#ifndef LUA_VIRTUAL_MACHINE_H
#define LUA_VIRTUAL_MACHINE_H

#include "common.h"
#include "conf.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

struct lua_VM {
    Chunk *chunk;
    Byte *ip; // Byte instruction pointer into `chunk`.
    TValue stack[LUA_VM_STACKSIZE]; // Hardcoded limit for simplicity.
    TValue *bp; // Base pointer for current function.
    TValue *sp; // Stack pointer to 1 past the last element.
    Table globals; // Interned global variable identifiers, as strings.
    Table strings; // Interned string literals/user-created ones.
    lua_Object *objects; // Head of linked list of allocated objects.
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

#endif /* LUA_VIRTUAL_MACHINE_H */
