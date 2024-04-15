#include <stdarg.h>
#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "limits.h"

enum RT_ErrType {
    RT_ERROR_NEGATE,
    RT_ERROR_ARITH,
    RT_ERROR_COMPARE,
};

static void reset_stack(VM *self) {
    self->top = self->stack;
}

static void runtime_error(VM *self, enum RT_ErrType rterr) {
    
// Errors occur with the guilty operands at the very top of the stack.
#define _get_typename(n)    get_typename(self->top + (n))

    size_t offset = self->ip - self->chunk->code - 1;
    int line = self->chunk->lines[offset];
    fprintf(stderr, "%s:%i: ", self->name, line);

    switch (rterr) {
    case RT_ERROR_NEGATE:
        fprintf(stderr, "Attempt to negate a %s value", _get_typename(-1));
        break;
    case RT_ERROR_ARITH:
        fprintf(stderr,
                "Attempt to perform arithmetic on %s with %s",
                _get_typename(-2),
                _get_typename(-1));
        break;
    case RT_ERROR_COMPARE:
        fprintf(stderr,
                "Attempt to compare %s with %s",
                _get_typename(-2),
                _get_typename(-1));
        break;
    }
    fputc('\n', stderr);
    reset_stack(self);
    longjmp(self->errorjmp, ERROR_RUNTIME);
    
#undef _get_typename

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

static ErrType run(VM *self) {
    const Chunk *chunk      = self->chunk;
    const TValue *constants = chunk->constants.values;

// --- HELPER MACROS ------------------------------------------------------ {{{1
// Many of these rely on variables local to this function.

#define read_byte()         (*self->ip++)

// Assumes MSB is read first then LSB.
#define read_byte2()        (decode_byte2(read_byte(), read_byte()))

// Assumes MSB is read first, then middle, then LSB.
#define read_byte3()        (decode_byte3(read_byte(), read_byte(), read_byte()))
    
// Assumes a 3-byte operand comes right after the opcode.
#define read_constant()     (&constants[read_byte3()])
#define poke_top(n)         (self->top + (n))
#define poke_base(n)        (self->stack + (n))
#define popn(n)             (self->top -= (n))

#define binary_op(opfn, setfn, errtype) {                                      \
    TValue *lhs = poke_top(-2);                                                \
    TValue *rhs = poke_top(-1);                                                \
    if (!is_number(lhs) || !is_number(rhs)) {                                  \
        runtime_error(self, errtype);                                          \
    }                                                                          \
    setfn(lhs, opfn(as_number(lhs), as_number(rhs)));                          \
    popn(1); \
}

// Remember that LHS would be pushed before RHS, so LHS is lower down the stack.
#define arith_op(fn)        binary_op(fn, set_number, RT_ERROR_ARITH)
#define compare_op(fn)      binary_op(fn, set_boolean, RT_ERROR_COMPARE)

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
        OpCode opcode = read_byte();
        switch (opcode) {
        case OP_CONSTANT:
            push_vm(self, read_constant());
            break;
        case OP_NIL:
            push_vm(self, &make_nil());
            break;
        case OP_TRUE:
            push_vm(self, &make_boolean(true));
            break;
        case OP_FALSE:
            push_vm(self, &make_boolean(false));
            break;
        case OP_EQ:
            set_boolean(poke_top(-2), values_equal(poke_top(-2), poke_top(-1)));
            popn(1);
            break;
        case OP_LT:
            compare_op(num_lt);
            break;
        case OP_LE:
            compare_op(num_le);
            break;
        case OP_ADD:
            arith_op(num_add);
            break;
        case OP_SUB:
            arith_op(num_sub);
            break;
        case OP_MUL:
            arith_op(num_mul);
            break;
        case OP_DIV:
            arith_op(num_div);
            break;
        case OP_MOD:
            arith_op(num_mod);
            break;
        case OP_POW:
            arith_op(num_pow);
            break;
        case OP_NOT:
            set_boolean(poke_top(-1), is_falsy(poke_top(-1)));
            break;
        case OP_UNM:
            if (!is_number(poke_top(-1))) {
                runtime_error(self, RT_ERROR_NEGATE); // throws
            }
            set_number(poke_top(-1), num_unm(as_number(poke_top(-1))));
            break;
        case OP_RETURN:
            print_value(poke_top(-1));
            printf("\n\n");
            popn(1);
            return ERROR_NONE;
        }
    }

#undef read_byte
#undef read_constant
#undef poke_top
#undef poke_base
#undef popn
#undef binary_op
#undef arith_op
#undef compare_op
}

ErrType interpret(VM *self, const char *input) {
    Chunk chunk;
    Lexer lexer;
    Compiler compiler;
    ErrType err = setjmp(self->errorjmp); // WARNING: Call `longjmp` correctly!!
    switch (err) {
    case ERROR_NONE:
        init_chunk(&chunk, self->name);
        init_compiler(&compiler, &lexer, self);
        compile(&compiler, input, &chunk);
        break;
    case ERROR_COMPTIME: // Fall through
    case ERROR_RUNTIME:
    default: // WARNING: Should not happen! Check all uses of `(set|long)jmp`.
        free_chunk(&chunk);
        return err;
    }
    // Prep the VM
    self->chunk = &chunk;
    self->ip    = chunk.code;
    ErrType result = run(self);
    free_chunk(&chunk);
    return result;
}
