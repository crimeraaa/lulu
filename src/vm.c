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

// Determine if the instruction's opcode B-mode matches the given `mode`.
static bool check_BMode(Instruction instruction, enum OpArgMask mode) {
    OpCode opcode = GET_OPCODE(instruction);
    enum OpArgMask opmode = get_BMode(opcode);
    return opmode == mode;
}

static bool check_CMode(Instruction instruction, enum OpArgMask mode) {
    OpCode opcode = GET_OPCODE(instruction);
    enum OpArgMask opmode = get_CMode(opcode);
    return opmode == mode;
}

static const TValue *read_constant(const TValue *Kst, Instruction instruction) {
    int index = GETARG_RBx(instruction);
    const TValue *constant = &Kst[index];
    return constant;
}

static const TValue *read_rkb(TValue *base, const TValue *Kst, Instruction instruction) {
    int rb = GETARG_RB(instruction); // May be register or constant index.
    if (ISK(rb)) {
        int index = INDEXK(rb);
        return &Kst[index];
    } else {
        return &base[rb];
    }
}

static const TValue *read_rkc(TValue *base, const TValue *Kst, Instruction instruction) {
    int rc = GETARG_RC(instruction); // May be register or constant index.
    if (ISK(rc)) {
        int index = INDEXK(rc);
        return &Kst[index];
    } else {
        return &base[rc];
    }
}

static bool is_constant_c(Instruction instruction) {
    int rc = GETARG_RC(instruction);
    return ISK(rc);
}

static InterpretResult run(lua_VM *self) {
    // Base pointer for current calling frame. For now assume stack base.
    TValue *base = self->stack;
    // Constants table.
    const TValue *Kst = self->chunk->constants.values;
    
// HELPER MACROS ---------------------------------------------------------- {{{1
// NOTE: Many of these rely on implied variable names, e.g. `Kst` and `base`.

#define READ_INSTRUCTION()      (*self->ip++)
#define RA(instruction)         (base + GETARG_RA(instruction))
#define RB(instruction) \
    check_exp(check_BMode(instruction, OpArgR), base + GETARG_RB(instruction))
#define RC(instruction) \
    check_exp(check_CMode(instruction, OpArgR), base + GETARG_RC(instruction))

/* Read a register index or a constant index from argument B. */
#define RKB(instruction) \
    check_exp(check_BMode(instruction, OpArgK), read_rkb(base, Kst, instruction))

/* Read a register index or a constant index from argument C. */
#define RKC(instruction) \
    check_exp(check_CMode(instruction, OpArgK), read_rkc(base, Kst, instruction))

/* Load a constant value from the constants table. May assert. */
#define KBx(instruction) \
    check_exp(check_BMode(instruction, OpArgK), read_constant(Kst, instruction))

/* WARNING: Likely dangerous to assume we should pop if C is a register. */
#define arith_op(op) {                                                         \
    const TValue *rkb = RKB(instruction);                                      \
    const TValue *rkc = RKC(instruction);                                      \
    lua_Number lhs = asnumber(rkb);                                            \
    lua_Number rhs = asnumber(rkc);                                            \
    setnumber(ra, op(lhs, rhs));                                               \
    if (!is_constant_c(instruction)) {                                         \
        self->top--;                                                           \
    }                                                                          \
}
    
// 1}}} ------------------------------------------------------------------------

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
        OpCode opcode = GET_OPCODE(instruction);
        TValue *ra = RA(instruction);
        switch (opcode) {
            case OP_CONSTANT: {
                // Possibly dangerous to assume we need to increment.
                self->top++;
                const TValue *constant = KBx(instruction);
                setobj(ra, constant);
            } break;
            // All uses of `arith_op` rely on specifically named variables.
            case OP_ADD:
                arith_op(luai_numadd);
                break;
            case OP_SUB:
                arith_op(luai_numsub);
                break;
            case OP_MUL: 
                arith_op(luai_nummul);
                break;
            case OP_DIV:
                arith_op(luai_numdiv);
                break;
            case OP_MOD:
                arith_op(luai_nummod);
                break;
            case OP_POW:
                arith_op(luai_numpow);
                break;
            case OP_UNM: {
                TValue *rb = RB(instruction);
                lua_Number nb = asnumber(rb);
                setnumber(ra, luai_numunm(nb));
            } break;
            case OP_RETURN: {
                int b = GETARG_RB(instruction);
                // NOTE: This might break easily later on
                for (int i = 0; i < b; i++) {
                    print_value(ra + i);
                }
                printf("\n");
            } return INTERPRET_OK;
            default: {
                printf("Unknown opcode %i.\n", cast(int, opcode));
            } return INTERPRET_RUNTIME_ERROR;
        }
    }
    return INTERPRET_RUNTIME_ERROR;
}

#undef READ_INSTRUCTION
#undef RA
#undef RB
#undef RC
#undef RKB
#undef RKB
#undef KBx
#undef arith_op

InterpretResult interpret(lua_VM *self, Chunk *chunk) {
    self->chunk = chunk;
    self->ip    = chunk->code;
    reset_stack(self);
    return run(self);
}
