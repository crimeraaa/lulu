#include <stdarg.h>
#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "limits.h"
#include "memory.h"

enum RT_ErrType {
    RT_ERROR_NEGATE,
    RT_ERROR_ARITH,
    RT_ERROR_COMPARE,
    RT_ERROR_CONCAT,
    RT_ERROR_UNDEF,   // Make sure to push the invalid variable name first.
};

static void *vm_allocfn(void *ptr, size_t oldsz, size_t newsz, void *context) {
    VM *self  = cast(VM*, context);
    void *res = realloc(ptr, newsz);
    unused(oldsz);
    if (res == NULL) {
        logprintln("[FATAL ERROR]: No more memory");
        longjmp(self->errorjmp, ERROR_ALLOC);
    }
    return res;
}

static void vm_deallocfn(void *ptr, size_t size, void *context) {
    VM *self = cast(VM*, context);
    unused2(size, self);
    free(ptr);
}

static void reset_stack(VM *self) {
    self->top = self->stack;
}

static void runtime_error(VM *self, enum RT_ErrType rterr) {

// Errors occur with the guilty operands at the very top of the stack.
#define _typename(n)    get_typename(self->top + (n))

    size_t offset = self->ip - self->chunk->code - 1;
    int line = self->chunk->lines[offset];
    fprintf(stderr, "%s:%i: ", self->name, line);

    switch (rterr) {
    case RT_ERROR_NEGATE:
        fprintf(stderr,
                "Attempt to negate a %s value",
                _typename(-1));
        break;
    case RT_ERROR_ARITH:
        fprintf(stderr,
                "Attempt to perform arithmetic on %s with %s",
                _typename(-2),
                _typename(-1));
        break;
    case RT_ERROR_COMPARE:
        fprintf(stderr,
                "Attempt to compare %s with %s",
                _typename(-2),
                _typename(-1));
        break;
    case RT_ERROR_CONCAT:
        fprintf(stderr,
                "Attempt to concatenate %s with %s",
                _typename(-2),
                _typename(-1));
        break;
    case RT_ERROR_UNDEF:
        fprintf(stderr,
                "Attempt to read undefined variable '%s'.",
                as_cstring(self->top - 1));
        break;
    }
    fputc('\n', stderr);
    reset_stack(self);
    longjmp(self->errorjmp, ERROR_RUNTIME);

#undef _typename

}

void init_vm(VM *self, const char *name) {
    reset_stack(self);
    init_allocator(&self->allocator, vm_allocfn, vm_deallocfn, self);
    init_table(&self->globals);
    init_table(&self->strings);
    self->name    = name;
    self->objects = NULL;
}

void free_vm(VM *self) {
    Allocator *allocator = &self->allocator;
    free_table(&self->globals, allocator);
    free_table(&self->strings, allocator);
    free_objects(self);
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
    Allocator *allocator    = &self->allocator;
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
#define read_string()       as_string(read_constant())
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
    popn(1);                                                                   \
}

// Remember that LHS would be pushed before RHS, so LHS is lower down the stack.
#define arith_op(fn)        binary_op(fn, set_number,  RT_ERROR_ARITH)
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
        case OP_POP:
            popn(1);
            break;
        case OP_GETGLOBAL: {
            // Assume this is a string for the variable's name.
            const TValue *name = read_constant();
            TValue value;
            if (!get_table(&self->globals, name, &value)) {
                push_vm(self, name);
                runtime_error(self, RT_ERROR_UNDEF);
            }
            push_vm(self, &value);
        } break;
        case OP_SETGLOBAL:
            // Same as `OP_GETGLOBAL`.
            set_table(&self->globals, read_constant(), poke_top(-1), allocator);
            popn(1);
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
        case OP_CONCAT:
            if (!is_string(poke_top(-2)) || !is_string(poke_top(-1))) {
                runtime_error(self, RT_ERROR_CONCAT); // throws
            } else {
                TString *ts = concat_strings(self,
                                             as_string(poke_top(-2)),
                                             as_string(poke_top(-1)));
                popn(2);
                push_vm(self, &make_string(ts));
            }
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
        case OP_PRINT:
            print_value(poke_top(-1));
            printf("\n");
            popn(1);
            break;
        case OP_RETURN:
            return ERROR_NONE;
        }
    }

#undef read_byte
#undef read_byte2
#undef read_byte3
#undef read_constant
#undef read_string
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
    case ERROR_ALLOC:
    default:   // WARNING: Should not happen! Check all uses of `(set|long)jmp`.
        free_chunk(&chunk, &self->allocator);
        return err;
    }
    // Prep the VM
    self->chunk = &chunk;
    self->ip    = chunk.code;
    err = run(self);
    free_chunk(&chunk, &self->allocator);
    return err;
}
