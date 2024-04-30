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
    RTE_INDEX,   // Using `[]` on non-table values.
};

// Assumes that `context` points to the parent `VM*` instance.
static void *allocfn(void *ptr, size_t oldsz, size_t newsz, void *context) {
    unused(oldsz);
    VM *self = context;
    if (newsz == 0) {
        free(ptr);
        return NULL;
    }
    void *res = realloc(ptr, newsz);
    if (res == NULL) {
        logprintln("[FATAL ERROR]: No more memory");
        longjmp(self->errorjmp, ERROR_ALLOC);
    }
    return res;
}

static void reset_stack(VM *self) {
    self->top = self->stack;
}

static void runtime_error(VM *self, enum RT_ErrType rterr) {

// Errors occur with the guilty operands at the very top of the stack.
#define _type(n)    get_typename(self->top + (n))
#define _cstr(n)    as_cstring(self->top + (n))

    size_t offset = self->ip - self->chunk->code - 1;
    int    line   = self->chunk->lines[offset];
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
    case RTE_INDEX:
        eprintf("Attempt to index a %s value", _type(-1));
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
    init_alloc(&self->alloc, allocfn, self);
    init_table(&self->globals);
    init_table(&self->strings);
    self->name    = name;
    self->objects = NULL;
}

void free_vm(VM *self) {
    Alloc *alloc = &self->alloc;
    // dump_table(&self->globals, ".globals");
    // dump_table(&self->strings, ".strings");
    free_table(&self->globals, alloc);
    free_table(&self->strings, alloc);
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
    Alloc  *alloc     = &self->alloc;
    Chunk  *chunk     = self->chunk;
    TValue *constants = chunk->constants.values;

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
            for (int i = 0, limit = read_byte(); i < limit; i++) {
                push_back(&make_nil());
            }
            break;
        case OP_TRUE:
            push_back(&make_boolean(true));
            break;
        case OP_FALSE:
            push_back(&make_boolean(false));
            break;
        case OP_POP:
            popn(read_byte());
            break;
        case OP_GETLOCAL:
            push_back(&self->stack[read_byte()]);
            break;
        case OP_GETGLOBAL: {
            // Assume this is a string for the variable's name.
            TValue *name = read_constant();
            TValue  value;
            if (!get_table(&self->globals, name, &value)) {
                push_back(name);
                runtime_error(self, RTE_UNDEF);
            }
            push_back(&value);
            break;
        }
        case OP_GETTABLE:
            if (!is_table(poke_top(-2))) {
                // Push the guilty variable to the top so we can report it.
                TValue *bad = poke_top(-2);
                push_back(bad);
                runtime_error(self, RTE_INDEX);
            } else {
                Table  *table = as_table(poke_top(-2));
                TValue *key   = poke_top(-1);
                TValue  value;
                if (!get_table(table, key, &value)) {
                    set_nil(&value);
                }
                popn(2);
                push_back(&value);
            }
            break;
        case OP_SETLOCAL:
            self->stack[read_byte()] = *poke_top(-1);
            pop_back();
            break;
        case OP_SETGLOBAL:
            // Same as `OP_GETGLOBAL`.
            set_table(&self->globals, read_constant(), poke_top(-1), alloc);
            pop_back();
            break;
        case OP_SETTABLE: {
            int     index  = read_byte(); // Absolute index of table itself.
            int     popped = read_byte();
            TValue *tbl    = poke_base(index);
            TValue *key    = poke_base(index + 1);
            TValue *val    = poke_top(-1);

            if (!is_table(tbl)) {
                // Push the guilty variable to the top so we can report it.
                push_back(tbl);
                runtime_error(self, RTE_INDEX);
            } else {
                set_table(as_table(tbl), key, val, alloc);
                popn(popped);
            }
            break;
        }
        case OP_EQ: {
            TValue *lhs = poke_top(-2);
            TValue *rhs = poke_top(-1);
            set_boolean(lhs, values_equal(lhs, rhs));
            pop_back();
            break;
        }
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
            int      argc = read_byte();
            TValue  *argv = poke_top(-argc);
            TString *res  = try_concat(self, argc, argv);
            popn(argc);
            push_back(&make_string(res));
            break;
        }
        case OP_UNM: {
            TValue *arg = poke_top(-1);
            if (!is_number(arg)) {
                runtime_error(self, RTE_NEGATE); // throws
            }
            set_number(arg, num_unm(as_number(arg)));
            break;
        }
        case OP_NOT: {
            TValue *arg = poke_top(-1);
            set_boolean(arg, is_falsy(arg));
            break;
        }
        case OP_LEN: {
            // TODO: Add support for tables, will need to implement arrays then
            TValue *arg = poke_top(-1);
            if (!is_string(arg)) {
                runtime_error(self, RTE_LENGTH);
            }
            set_number(arg, as_string(arg)->len);
            break;
        }
        case OP_PRINT: {
            int     argc = read_byte();
            TValue *argv = poke_top(-argc);
            for (int i = 0; i < argc; i++) {
                print_value(&argv[i]);
                printf("\t");
            }
            printf("\n");
            self->top = argv;
        } break;
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
        // For the default case, please ensure all calls of `longjmp` ONLY
        // ever pass an `ErrType` member.
        free_chunk(&chunk, &self->alloc);
        return err;
    }
    // Prep the VM
    self->chunk = &chunk;
    self->ip    = chunk.code;
    err = run(self);
    free_chunk(&chunk, &self->alloc);
    return err;
}
