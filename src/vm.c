#include "api.h"
#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "limits.h"
#include "memory.h"
#include "string.h"
#include "table.h"

// Assumes that the context pointer points to the parent `VM*` instance.
static void *allocate(void *ptr, size_t oldsz, size_t newsz, void *ctx)
{
    unused(oldsz);
    VM *vm = ctx;
    if (newsz == 0) {
        free(ptr);
        return NULL;
    }
    void *res = realloc(ptr, newsz);
    if (res == NULL) {
        lulu_alloc_error(vm);
    }
    return res;
}

static void reset_stack(VM *vm)
{
    vm->top  = vm->stack;
    vm->base = vm->stack;
}

static void init_builtin(VM *vm)
{
    lulu_push_table(vm, &vm->globals);
    lulu_set_global(vm, "_G");
}

// Silly but need this as stack-allocated tables don't init their objects.
static void _init_table(lulu_VM *vm, Table *t)
{
    t->object.tag = TYPE_TABLE;
    init_table(vm, t);
}

void init_vm(VM *vm)
{
    reset_stack(vm);
    lulu_set_allocator(vm, &allocate, NULL);
    _init_table(vm, &vm->globals);
    _init_table(vm, &vm->strings);
    vm->objects = NULL;

    // This must occur AFTER the strings table and objects list are initialized.
    init_builtin(vm);
}

void free_vm(VM *vm)
{
    free_table(vm, &vm->globals);
    free_table(vm, &vm->strings);
    lulu_free_objects(vm);
}

typedef enum {
    TM_ADD, TM_SUB, TM_MUL, TM_DIV, TM_MOD, TM_POW, TM_UNM,
    TM_LT,  TM_LE,
} TagMethod;

const char *pick_non_number(const Value *a, const Value *b)
{
    Value tmp;
    // First operand is wrong?
    if (!value_tonumber(a, &tmp))
        return get_typename(a);
    return get_typename(b);
}

static void arith_tm(VM *vm, Value *a, const Value *b, TagMethod tm)
{
    Value tmp_a;
    Value tmp_b;
    if (value_tonumber(a, &tmp_a) && value_tonumber(b, &tmp_b)) {
        Number na = as_number(&tmp_a);
        Number nb = as_number(&tmp_b);
        switch (tm) {
        case TM_ADD: setv_number(a, num_add(na, nb)); break;
        case TM_SUB: setv_number(a, num_sub(na, nb)); break;
        case TM_MUL: setv_number(a, num_mul(na, nb)); break;
        case TM_DIV: setv_number(a, num_div(na, nb)); break;
        case TM_MOD: setv_number(a, num_mod(na, nb)); break;
        case TM_POW: setv_number(a, num_pow(na, nb)); break;
        case TM_UNM: setv_number(a, num_unm(na));     break;
        default:
            // Should be unreachable.
            assert(false);
            break;
        }
    } else {
        lulu_type_error(vm, "perform arithmetic on", pick_non_number(a, b));
    }
}

static void compare_tm(VM *vm, Value *a, const Value *b, TagMethod tm)
{
    // Lua does implement comparison when both operands are the same, and by
    // default they allow string comparisons.
    unused(tm);
    lulu_type_error(vm, "compare", pick_non_number(a, b));
}

static lulu_ErrorCode run(VM *vm)
{
    Chunk *ck  = vm->chunk;
    Value *kst = ck->constants.values;

// --- HELPER MACROS ------------------------------------------------------ {{{1
// Many of these rely on variables local to this function.

#define read_byte()         (*vm->ip++)

// Assumes MSB is read first then LSB.
#define read_byte2()        (decode_byte2(read_byte(), read_byte()))

// Assumes MSB is read first, then middle, then LSB.
#define read_byte3()        (encode_byte3(read_byte(), read_byte(), read_byte()))

// Assumes a 3-byte operand comes right after the opcode.
#define read_constant()     (&kst[read_byte3()])
#define read_string()       as_string(read_constant())

#define binary_op_or_tm(set_fn, op_fn, tm_fn, tm)                              \
{                                                                              \
    Value *a = poke_top(vm, -2);                                               \
    Value *b = poke_top(vm, -1);                                               \
    if (is_number(a) && is_number(b)) {                                        \
        Number na = as_number(a);                                              \
        Number nb = as_number(b);                                              \
        set_fn(a, op_fn(na, nb));                                              \
    } else {                                                                   \
        tm_fn(vm, a, b, tm);                                                   \
    }                                                                          \
    lulu_pop(vm, 1);                                                           \
}

#define arith_op_or_tm(op_fn, tm) \
    binary_op_or_tm(setv_number, op_fn, arith_tm, tm)

#define compare_op_or_tm(op_fn, tm) \
    binary_op_or_tm(setv_boolean, op_fn, compare_tm, tm)

// 1}}} ------------------------------------------------------------------------

    for (;;) {
        if (is_enabled(DEBUG_TRACE_EXECUTION)) {
            printf("\t");
            for (const Value *slot = vm->stack; slot < vm->top; slot++) {
                printf("[ ");
                print_value(slot, true);
                printf(" ]");
            }
            printf("\n");
            disassemble_instruction(ck, cast(int, vm->ip - ck->code));
        }
        OpCode op = read_byte();
        switch (op) {
        case OP_CONSTANT:
            push_back(vm, read_constant());
            break;
        case OP_NIL:
            lulu_push_nil(vm, read_byte());
            break;
        case OP_TRUE:
            lulu_push_boolean(vm, true);
            break;
        case OP_FALSE:
            lulu_push_boolean(vm, false);
            break;
        case OP_POP:
            lulu_pop(vm, cast(int, read_byte()));
            break;
        case OP_NEWTABLE:
            lulu_push_table(vm, new_table(vm, read_byte3()));
            break;
        case OP_GETLOCAL:
            push_back(vm, &vm->base[read_byte()]);
            break;
        case OP_GETGLOBAL: {
            String *s = read_string();
            lulu_get_global(vm, s);
            break;
        }
        case OP_GETTABLE:
            lulu_get_table(vm, -2, -1);
            break;
        case OP_SETLOCAL:
            vm->base[read_byte()] = *poke_top(vm, -1);
            lulu_pop(vm, 1);
            break;
        case OP_SETGLOBAL: {
            String *s = read_string();
            lulu_set_global(vm, s);
            break;
        }
        case OP_SETTABLE: {
            int t_offset = read_byte();
            int k_offset = read_byte();
            int poppable = read_byte();
            lulu_set_table(vm, t_offset, k_offset, poppable);
            break;
        }
        case OP_SETARRAY: {
            int     t_idx = read_byte(); // Absolute index of the table.
            int     count = read_byte(); // How many elements in the array?
            Table  *t     = as_table(poke_base(vm, t_idx));

            // Remember: Lua uses 1-based indexing!
            for (int i = 1; i <= count; i++) {
                Value  k = make_number(i);
                Value *v = poke_base(vm, t_idx + i);
                set_table(vm, t, &k, v);
            }
            lulu_pop(vm, count);
            break;
        }
        case OP_EQ: {
            Value *a = poke_top(vm, -2);
            Value *b = poke_top(vm, -1);
            setv_boolean(a, values_equal(a, b));
            lulu_pop(vm, 1);
            break;
        }
        case OP_LT:  compare_op_or_tm(num_lt, TM_LT); break;
        case OP_LE:  compare_op_or_tm(num_le, TM_LE); break;
        case OP_ADD: arith_op_or_tm(num_add, TM_ADD); break;
        case OP_SUB: arith_op_or_tm(num_sub, TM_SUB); break;
        case OP_MUL: arith_op_or_tm(num_mul, TM_MUL); break;
        case OP_DIV: arith_op_or_tm(num_div, TM_DIV); break;
        case OP_MOD: arith_op_or_tm(num_mod, TM_MOD); break;
        case OP_POW: arith_op_or_tm(num_pow, TM_POW); break;
        case OP_CONCAT:
            // Assume at least 2 args since concat is an infix expression.
            lulu_concat(vm, read_byte());
            break;
        case OP_UNM: {
            Value *arg = poke_top(vm, -1);
            if (is_number(arg))
                setv_number(arg, num_unm(as_number(arg)));
            else
                arith_tm(vm, arg, arg, TM_UNM);
            break;
        }
        case OP_NOT:
            lulu_to_boolean(vm, -1);
            break;
        case OP_LEN: {
            // TODO: Separate array segment from hash segment of tables.
            Value *arg = poke_top(vm, -1);
            if (!is_string(arg))
                lulu_type_error(vm, "get length of", get_typename(arg));
            setv_number(arg, as_string(arg)->len);
            break;
        }
        case OP_PRINT: {
            int argc = read_byte();
            for (int i = 0; i < argc; i++) {
                if (i > 0)
                    fputs("\t", stdout);
                fputs(lulu_to_cstring(vm, i - argc), stdout);
            }
            fputs("\n", stdout);
            lulu_pop(vm, argc);
            break;
        }
        case OP_RETURN:
            return LULU_ERROR_NONE;
        }
    }

#undef read_byte
#undef read_byte2
#undef read_byte3
#undef read_constant
#undef read_string
}

lulu_ErrorCode interpret(VM *vm, const char *input)
{
    Chunk    ck;
    Lexer    ls;
    Compiler cpl;
    lulu_ErrorCode err = setjmp(vm->errorjmp);
    switch (err) {
    case LULU_ERROR_NONE:
        init_chunk(vm, &ck, vm->name);
        init_compiler(&cpl, &ls, vm);
        compile(&cpl, input, &ck);
        break;
    case LULU_ERROR_RUNTIME:
    case LULU_ERROR_COMPTIME:
    case LULU_ERROR_ALLOC:
        // For the default case, please ensure all calls of `longjmp` ONLY
        // ever pass an `lulu_ErrorCode` member.
        free_chunk(vm, &ck);
        return err;
    }
    // Prep the VM
    vm->chunk = &ck;
    vm->ip    = ck.code;
    err       = run(vm);
    free_chunk(vm, &ck);
    return err;
}
