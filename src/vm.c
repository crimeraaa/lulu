#include "vm.h"
#include "opcodes.h"
#include "compiler.h"
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
    int index = GETARG_Bx(instruction);
    const TValue *constant = &Kst[index];
    return constant;
}

static const TValue *read_rkb(TValue *base, const TValue *Kst, Instruction instruction) {
    int rb = GETARG_B(instruction); // May be register or constant index.
    if (ISK(rb)) {
        int index = INDEXK(rb);
        return &Kst[index];
    } else {
        return &base[rb];
    }
}

static const TValue *read_rkc(TValue *base, const TValue *Kst, Instruction instruction) {
    int rc = GETARG_C(instruction); // May be register or constant index.
    if (ISK(rc)) {
        int index = INDEXK(rc);
        return &Kst[index];
    } else {
        return &base[rc];
    }
}

static bool is_poppable(Instruction instruction) {
    int rb = GETARG_B(instruction);
    int rc = GETARG_C(instruction);
    return !ISK(rb) && !ISK(rc);
}

static InterpretResult run(lua_VM *self) {
    // Base pointer for current calling frame. For now assume stack base.
    TValue *base = self->stack;
    // Constants table.
    const TValue *Kst = self->chunk->constants.values;
    
// HELPER MACROS ---------------------------------------------------------- {{{1
// NOTE: Many of these rely on implied variable names, e.g. `Kst` and `base`.

#define READ_INSTRUCTION()      (*self->ip++)
#define RA(instruction)         (base + GETARG_A(instruction))
#define RB(instruction) \
    check_exp(check_BMode(instruction, OpArgR), base + GETARG_B(instruction))
#define RC(instruction) \
    check_exp(check_CMode(instruction, OpArgR), base + GETARG_C(instruction))

/* Read a register index or a constant index from argument B. */
#define RKB(instruction) \
    check_exp(check_BMode(instruction, OpArgK), read_rkb(base, Kst, instruction))

/* Read a register index or a constant index from argument C. */
#define RKC(instruction) \
    check_exp(check_CMode(instruction, OpArgK), read_rkc(base, Kst, instruction))

/* Load a constant value from the constants table. May assert. */
#define KBx(instruction) \
    check_exp(check_BMode(instruction, OpArgK), read_constant(Kst, instruction))

/* WARNING: Makes a dangerous assumption that we need to pop when both args are 
registers. This assumes that we pushed some arguments and need to pop them. */
#define arith_op(op) {                                                         \
    const TValue *rkb = RKB(instruction);                                      \
    const TValue *rkc = RKC(instruction);                                      \
    lua_Number lhs = asnumber(rkb);                                            \
    lua_Number rhs = asnumber(rkc);                                            \
    setnumber(ra, op(lhs, rhs));                                               \
    if (is_poppable(instruction)) self->top--;                                 \
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
                // Only increment if we're increasing the current number of 
                // active values. However 'dead' values are still in the stack.
                if (ra >= self->top) {
                    self->top++;
                }
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
                int b = GETARG_B(instruction);
                // NOTE: This might break easily later on
                for (int i = 0; i < b; i++) {
                    print_value(ra + i);
                }
                // Set top of stack to point to before the first return value.
                if (b != 0) {
                    self->top = ra + b - 1;
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

InterpretResult interpret(lua_VM *self, const char *input) {
    unused(self);
    Compiler *compiler = &(Compiler){0};
    compiler->lexstate = &(LexState){0};
    compile(compiler, input);
    return INTERPRET_OK;
}
