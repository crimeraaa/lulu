#include <stdarg.h>
#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "limits.h"
#include "memory.h"

enum RT_ErrType {
    RTE_NEGATE,
    RTE_ARITH,
    RTE_COMPARE,
    RTE_CONCAT,
    RTE_UNDEF,   // Make sure to push_back the invalid variable name first.
    RTE_LENGTH,  // Using `#` on non-table and non-string values.
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
#define _type(n)    get_typename(self->top + (n))
#define _cstr(n)    as_cstring(self->top + (n))

    size_t offset = self->ip - self->chunk->code - 1;
    int line = self->chunk->lines[offset];
    fprintf(stderr, "%s:%i: ", self->name, line);

    switch (rterr) {
    case RTE_NEGATE:
        eprintf("Attempt to negate a %s value", _type(-1));
        break;
    case RTE_ARITH:
        eprintf("Attempt to perform arithmetic on %s with %s", _type(-2), _type(-1));
        break;
    case RTE_COMPARE:
        eprintf("Attempt to compare %s with %s", _type(-2), _type(-1));
        break;
    case RTE_CONCAT:
        eprintf("Attempt to concatenate %s with %s", _type(-2), _type(-1));
        break;
    case RTE_UNDEF:
        eprintf("Attempt to read undefined variable '%s'.", _cstr(-1));
        break;
    case RTE_LENGTH:
        eprintf("Attempt to get length of a %s value", _type(-1));
        break;
    }
    fputc('\n', stderr);
    reset_stack(self);
    longjmp(self->errorjmp, ERROR_RUNTIME);

#undef _type
#undef _cstr

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

static TString *try_concat(VM *self, int argc, const TValue argv[]) {
    int len = 0;
    for (int i = 0; i < argc; i++) {
        const TValue *arg = &argv[i];
        if (!is_string(arg)) {
            runtime_error(self, RTE_CONCAT);
        }
        len += as_string(arg)->len;
    }
    return concat_strings(self, argc, argv, len);
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
#define pop_back()          (popn(1))
#define push_back(v)        (*self->top++ = *(v))

// We can save a pop and push pair by just modifying the stack in-place.
#define binary_op(opfn, setfn, errtype) {                                      \
    TValue *lhs = poke_top(-2);                                                \
    TValue *rhs = poke_top(-1);                                                \
    if (!is_number(lhs) || !is_number(rhs)) {                                  \
        runtime_error(self, errtype);                                          \
    }                                                                          \
    setfn(lhs, opfn(as_number(lhs), as_number(rhs)));                          \
    pop_back();                                                                \
}

// Remember that LHS would be pushed before RHS, so LHS is lower down the stack.
#define arith_op(fn)        binary_op(fn, set_number,  RTE_ARITH)
#define compare_op(fn)      binary_op(fn, set_boolean, RTE_COMPARE)

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
            push_back(read_constant());
            break;
        case OP_NIL:
            push_back(&make_nil());
            break;
        case OP_TRUE:
            push_back(&make_boolean(true));
            break;
        case OP_FALSE:
            push_back(&make_boolean(false));
            break;
        case OP_POP:
            popn(read_byte3());
            break;
        case OP_GETGLOBAL: {
            // Assume this is a string for the variable's name.
            const TValue *name = read_constant();
            TValue value;
            if (!get_table(&self->globals, name, &value)) {
                push_back(name);
                runtime_error(self, RTE_UNDEF);
            }
            push_back(&value);
        } break;
        case OP_SETGLOBAL:
            // Same as `OP_GETGLOBAL`.
            set_table(&self->globals, read_constant(), poke_top(-1), allocator);
            pop_back();
            break;
        case OP_EQ: {
            TValue *lhs = poke_top(-2);
            TValue *rhs = poke_top(-1);
            set_boolean(lhs, values_equal(lhs, rhs));
            pop_back();
        } break;
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
        case OP_CONCAT: {
            // Assume at least 2 args since concat is an infix expression.
            int argc = read_byte3();
            const TValue *argv = poke_top(-argc);
            TString *res = try_concat(self, argc, argv);
            popn(argc);
            push_back(&make_string(res));
        } break;
        case OP_UNM: {
            TValue *arg = poke_top(-1);
            if (!is_number(arg)) {
                runtime_error(self, RTE_NEGATE); // throws
            }
            set_number(arg, num_unm(as_number(arg)));
        } break;
        case OP_NOT: {
            TValue *arg = poke_top(-1);
            set_boolean(arg, is_falsy(arg));
        } break;
        case OP_LEN: {
            TValue *arg = poke_top(-1);
            if (!is_string(arg)) {
                runtime_error(self, RTE_LENGTH);
            }
            set_number(arg, as_string(arg)->len);
        } break;
        case OP_PRINT:
            print_value(poke_top(-1));
            printf("\n");
            pop_back();
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
#undef pop_back
#undef push_back
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
