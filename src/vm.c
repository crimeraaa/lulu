#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "limits.h"

static void reset_stack(VM *self) {
    self->top = self->stack;
}

void init_vm(VM *self, const char *name) {
    reset_stack(self);
    self->name = name;
}

void free_vm(VM *self) {
    unused(self);
}

void push_vm(VM *self, const TValue *value) {
    *self->top = *value;
    self->top++;
}

TValue pop_vm(VM *self) {
    self->top--;
    return *self->top;
}

static InterpretResult run(VM *self) {
    Instruction inst;
    const Chunk *chunk      = self->chunk;
    const TValue *constants = chunk->constants.values;

// --- HELPER MACROS ------------------------------------------------------ {{{1
// Many of these rely on variables local to this function.

#define read_instruction()      (*self->ip++)
#define read_constant()         (&constants[getarg_Bx(inst)])
#define poke_top(n)             (self->top + n)
#define poke_base(n)            (self->stack + n)
    
// Remember that LHS would be pushed before RHS, so it's lower down the stack.
#define arith_op(fn) { \
    TValue *lhs = poke_top(-2); \
    TValue *rhs = poke_top(-1); \
    set_number(lhs, fn(as_number(lhs), as_number(rhs))); \
    self->top--; \
}
    
// 1}}} ------------------------------------------------------------------------

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        printf("        ");
        for (const TValue *slot = self->stack; slot < self->top; slot++) {
            printf("[ "); 
            print_value(slot);
            printf(" ]");
        }
        printf("\n");
        disassemble_instruction(chunk, cast(int, self->ip - chunk->code));
#endif
        inst = read_instruction();        
        switch (getarg_op(inst)) {
        case OP_CONSTANT:
            push_vm(self, read_constant());
            break; 
        case OP_ADD:
            arith_op(lulu_numadd);
            break;
        case OP_SUB:
            arith_op(lulu_numsub);
            break;
        case OP_MUL:
            arith_op(lulu_nummul);
            break;
        case OP_DIV:
            arith_op(lulu_numdiv);
            break;
        case OP_MOD:
            arith_op(lulu_nummod);
            break;
        case OP_POW:
            arith_op(lulu_numpow);
            break;
        case OP_UNM: {
            TValue *value    = poke_top(-1);
            as_number(value) = lulu_numunm(as_number(value));
        } break;
        case OP_RETURN:
            print_value(poke_top(-1));
            printf("\n\n");
            self->top--;
            return INTERPRET_OK;
        }
    }

#undef read_instruction
#undef read_constant
#undef poke_top
#undef poke_base
#undef arith_op
}

InterpretResult interpret(VM *self, const char *input) {
    Chunk chunk;
    Lexer lexer;
    Compiler compiler;

    switch (setjmp(self->errorjmp)) {
    case 0:
        init_chunk(&chunk, "something");
        init_compiler(&compiler, &lexer, self);
        self->chunk = &chunk; 
        self->ip    = chunk.code;
        break;
    default:
        return INTERPRET_RUNTIME_ERROR;
    }
    compile(&compiler, input);
    return INTERPRET_OK;
}
