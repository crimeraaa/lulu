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
    Byte *ip;    // Instruction pointer (next instruction) in function's chunk.
    TValue *bp;  // Point into first slot of VM's values stack we can use.
} CallFrame;

struct LVM {
    TValue stack[LUA_MAXSTACK]; // Hardcoded limit for simplicity.
    CallFrame frames[LUA_MAXFRAMES];
    TValue _G; // Lua-facing version of the `globals` table.
    Table globals; // Interned global variable identifiers, as strings.
    Table strings; // Interned string literals/user-created ones.
    jmp_buf errjmp; // Unconditional jump when errors are triggered.
    CallFrame *cf; // Current calling stack frame as indexed into `frames`.
    TValue *bp;    // Pointer to base of current calling stack frame.
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

/* VM CALL FRAME MANIPULATION ------------------------------------------- {{{ */

/**
 * Read the current instruction and move the instruction pointer.
 *
 * Remember that postfix increment returns the original value of the expression.
 * So we effectively increment the pointer but we dereference the original one.
 */
#define lua_nextbyte(vm)        (*(vm)->cf->ip++)

/**
 * III:23.1     If Statements
 *
 * Read the next 2 instructions and combine them into a 16-bit operand.
 *
 * The compiler emitted the 2 byte operands for a jump instruction in order of
 * msb, lsb. So our instruction pointer points at msb currently.
 */
static inline Word readbyte2(LVM *self) {
    Byte msb = lua_nextbyte(self);
    Byte lsb = lua_nextbyte(self);
    return byteunmask(msb, 1) | lsb;
}

/**
 * Read the next 3 instructions and combine those 3 bytes into 1 24-bit operand.
 *
 * NOTE:
 *
 * This MUST be able to fit in a `DWord`.
 *
 * Compiler emitted them in this order: msb, mid, lsb. Since ip currently points
 * at msb, we can safely walk in this order.
 */
static inline DWord readbyte3(LVM *self) {
    Byte msb = lua_nextbyte(self);
    Byte mid = lua_nextbyte(self);
    Byte lsb = lua_nextbyte(self);
    return byteunmask(msb, 2) | byteunmask(mid, 1) | lsb;
}

/**
 * Read the next byte from the bytecode treating the received value as an index
 * into the VM's current chunk's constants pool.
 */
static inline TValue *readconstant_at(LVM *self, size_t index) {
    return &self->cf->function->chunk.constants.array[index];
}

static inline TValue *readconstant(LVM *self) {
    return readconstant_at(self, lua_nextbyte(self));
}

static inline TValue *readlconstant(LVM *self) {
    return readconstant_at(self, readbyte3(self));
}

/**
 * III:21.2     Variable Declarations
 *
 * Helper macro to read the current top of the stack and increment the VM's
 * instruction pointer and then cast the result to a `TString*`.
 */
#define readstring_at(vm, i)    asstring(readconstant_at(vm, i))
#define readstring(vm)          asstring(readconstant(vm))
#define readlstring(vm)         asstring(readlconstant(vm))

/* }}} ---------------------------------------------------------------------- */

#endif /* LUA_VIRTUAL_MACHINE_H */
