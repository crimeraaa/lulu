#include "vm.h"
#include "opcodes.h"
#include "debug.h"

static void reset_stack(lua_VM *self) {
    self->top = self->stack;
}

void init_vm(lua_VM *self) {
    reset_stack(self);
    self->chunk = NULL;
}

void free_vm(lua_VM *self) {
    unused(self);
}

void push_vm(lua_VM *self, const TValue *value) {
    *self->top = *value;
    self->top++;
}

TValue pop_vm(lua_VM *self) {
    self->top--;
    return *self->top;
}

// Determine if the instruction's opcode B-mode matches the given `mode`.
static bool check_BMode(Instruction instruction, enum OpArgMask mode) {
    OpCode opcode = GET_OPCODE(instruction);
    enum OpArgMask opmode = get_BMode(opcode);
    return opmode == mode;
}

static const TValue *read_constant(const TValue *Kst, Instruction instruction) {
    int index = GETARG_RBx(instruction);
    const TValue *constant = Kst + index;
    return constant;
}

static InterpretResult run(lua_VM *self) {
    // Base pointer for current calling frame. For now assume stack base.
    TValue *base = self->stack;
    // Constants table.
    const TValue *Kst = self->chunk->constants.values;

/* Common marcos for tasks under `run()`. Note the specific implied names. */
#define READ_INSTRUCTION()      (*self->ip++)
#define RA(instruction)         (base + GETARG_RA(instruction))
#define RB(instruction) \
    check_exp(check_BMode(instruction, OpArgR), base + GETARG_RB(instruction))
/* Load a constant value from the constants table. May assert. */
#define KBx(instruction) \
    check_exp(check_BMode(instruction, OpArgK), read_constant(Kst, instruction))

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (const TValue *slot = self->stack; slot < self->top; slot++) {
            printf("[ ");
            print_value(slot);
            printf(" ]");
        }
        printf("\n");
        disassemble_instruction(self->chunk, (int)(self->ip - self->chunk->code));
#endif
        Instruction instruction = READ_INSTRUCTION();
        TValue *ra = RA(instruction);
        switch (instruction) {
            case OP_CONSTANT: {
                // A different way of `pushing` values
                if (ra == self->top) {
                    self->top++;
                }
                setobj(ra, KBx(instruction));
                break;
            }
            case OP_UNM: {
                TValue *rb = RB(instruction);
                lua_Number nb = asnumber(rb);
                setnumber(ra, luai_numunm(nb));
                break;
            }
            case OP_RETURN: {
                const TValue popped = pop_vm(self);
                print_value(&popped);
                printf("\n");
                return INTERPRET_OK;
            }
        }
    }
    return INTERPRET_RUNTIME_ERROR;
}

#undef READ_INSTRUCTION
#undef RA
#undef RB
#undef KBx

InterpretResult interpret(lua_VM *self, Chunk *chunk) {
    self->chunk = chunk;
    self->ip    = chunk->code;
    return run(self);
}
