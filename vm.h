#ifndef LUA_VIRTUAL_MACHINE_H
#define LUA_VIRTUAL_MACHINE_H

#include "common.h"
#include "conf.h"
#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

/**
 * III:24.3.3   The call stack
 *
 * For each live function invocation we track where on the stack the locals begin
 * and where the caller should resume. This represents a single ongoing function
 * call.
 *
 * When we return from a function, the VM will jump to the `ip` of the caller's
 * `CallFrame` and resume from there.
 */
typedef struct {
    LFunction *function; // Contains our chunk, constants and other stuff.
    Byte *ip;       // Instruction pointer (next instruction) in function's chunk.
    TValue *bp;  // Point into first slot of VM's values stack we can use.
} CallFrame;

struct LVM {
    TValue stack[LUA_MAXSTACK]; // Hardcoded limit for simplicity.
    CallFrame frames[LUA_MAXFRAMES];
    Table globals; // Interned global variable identifiers, as strings.
    Table strings; // Interned string literals/user-created ones.
    jmp_buf errjmp; // Unconditional jump when errors are triggered.
    TValue *sp;    // Stack pointer to 1 past the lastest written element.
    Object *objects; // Head of intrusive linked list of allocated objects.
    const char *name; // Filename or `stdin`.
    const char *input; // Read-only pointer to malloc'd script contents.
    int fc; // Frame counter.
};

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void init_vm(LVM *self, const char *name);
void free_vm(LVM *self);

/**
 * Given a monolithic string of source code...
 */
InterpretResult interpret_vm(LVM *self, const char *source);

#endif /* LUA_VIRTUAL_MACHINE_H */
