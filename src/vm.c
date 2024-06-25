#include "api.h"
#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "limits.h"
#include "memory.h"
#include "string.h"
#include "table.h"

// Simple allocation wrapper using the C standard library.
static void *stdc_allocator(void *ptr, size_t oldsz, size_t newsz, void *ctx)
{
    unused2(oldsz, ctx);
    if (newsz == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, newsz);
}

static void reset_stack(lulu_VM *vm)
{
    vm->top  = vm->stack;
    vm->base = vm->stack;
}

static void init_builtin(lulu_VM *vm)
{
    lulu_push_table(vm, &vm->globals);
    lulu_set_global(vm, "_G");
}

// Silly but need this as stack-allocated tables don't init their objects.
static void _init_table(Table *t)
{
    t->object.tag  = TYPE_TABLE;
    t->object.next = NULL; // impossible to collect stack-allocated object
    luluTbl_init(t);
}

void luluVM_init(lulu_VM *vm)
{
    reset_stack(vm);
    luluMem_set_allocator(vm, &stdc_allocator, NULL);
    _init_table(&vm->globals);
    _init_table(&vm->strings);
    vm->objects = NULL;

    // This must occur AFTER the strings table and objects list are initialized.
    luluVal_intern_typenames(vm);
    luluLex_intern_tokens(vm);
    init_builtin(vm);
}

void luluVM_free(lulu_VM *vm)
{
    luluTbl_free(vm, &vm->globals);
    luluTbl_free(vm, &vm->strings);
    luluObj_free_all(vm);
}

typedef enum {
    TM_ADD, TM_SUB, TM_MUL, TM_DIV, TM_MOD, TM_POW, TM_UNM,
    TM_LT,  TM_LE,
} TagMethod;

const char *pick_non_number(StackID a, StackID b)
{
    // First operand is wrong?
    if (!luluVal_to_number(a).ok)
        return get_typename(a);
    return get_typename(b);
}

static void arith_tm(lulu_VM *vm, StackID a, StackID b, TagMethod tm)
{
    ToNumber ca = luluVal_to_number(a);
    ToNumber cb = luluVal_to_number(b);
    if (ca.ok && cb.ok) {
        switch (tm) {
        case TM_ADD: setv_number(a, lulu_num_add(ca.number, cb.number)); break;
        case TM_SUB: setv_number(a, lulu_num_sub(ca.number, cb.number)); break;
        case TM_MUL: setv_number(a, lulu_num_mul(ca.number, cb.number)); break;
        case TM_DIV: setv_number(a, lulu_num_div(ca.number, cb.number)); break;
        case TM_MOD: setv_number(a, lulu_num_mod(ca.number, cb.number)); break;
        case TM_POW: setv_number(a, lulu_num_pow(ca.number, cb.number)); break;
        case TM_UNM: setv_number(a, lulu_num_unm(ca.number));            break;
        default:
            // Should be unreachable.
            assert(false);
            break;
        }
    } else {
        lulu_type_error(vm, "perform arithmetic on", pick_non_number(a, b));
    }
}

static void compare_tm(lulu_VM *vm, StackID a, StackID b, TagMethod tm)
{
    // Lua does implement comparison when both operands are the same, and by
    // default they allow string comparisons.
    unused(tm);
    lulu_type_error(vm, "compare", pick_non_number(a, b));
}

static bool to_number(StackID id)
{
    ToNumber conv = luluVal_to_number(id);
    if (conv.ok)
        setv_number(id, conv.number);
    return conv.ok;
}

lulu_Status luluVM_execute(lulu_VM *vm)
{
    Chunk *ck  = vm->chunk;
    Value *kst = ck->constants.values;

// --- HELPER MACROS ------------------------------------------------------ {{{1
// Many of these rely on variables local to this function.
// `read_byte2` assumes MSB is read first, then LSB.
// `read_byte3` assumes MSB is read first, then middle, then LSB.

#define read_byte()         (*vm->ip++)
#define read_byte2()        (encode_byte2(read_byte(), read_byte()))
#define read_byte3()        (encode_byte3(read_byte(), read_byte(), read_byte()))
#define read_constant()     (&kst[read_byte3()])
#define read_string()       as_string(read_constant())

#define binary_op_or_tm(set_fn, op_fn, tm_fn, tm)                              \
{                                                                              \
    StackID a = poke_top(vm, -2);                                              \
    StackID b = poke_top(vm, -1);                                              \
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
            if (vm->top != vm->stack)
                luluDbg_print_stack(vm);
            luluDbg_disassemble_instruction(ck, cast_int(vm->ip - ck->code));
        }
        OpCode  op    = read_byte();
        StackID arg_a = poke_top(vm, -1); // Used a ton here.
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
            lulu_pop(vm, cast_int(read_byte()));
            break;
        case OP_NEWTABLE:
            lulu_push_table(vm, luluTbl_new(vm, read_byte3()));
            break;
        case OP_GETLOCAL:
            push_back(vm, &vm->base[read_byte()]);
            break;
        case OP_GETGLOBAL:
            lulu_get_global(vm, read_string());
            break;
        case OP_GETTABLE:
            lulu_get_table(vm, -2, -1);
            break;
        case OP_SETLOCAL:
            vm->base[read_byte()] = *arg_a;
            lulu_pop(vm, 1);
            break;
        case OP_SETGLOBAL:
            lulu_set_global(vm, read_string());
            break;
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
                Value   k = make_number(i);
                StackID v = poke_base(vm, t_idx + i);
                luluTbl_set(vm, t, &k, v);
            }
            lulu_pop(vm, count);
            break;
        }
        case OP_EQ: {
            StackID a = poke_top(vm, -2);
            StackID b = arg_a;
            setv_boolean(a, luluVal_equal(a, b));
            lulu_pop(vm, 1);
            break;
        }
        case OP_LT:  compare_op_or_tm(lulu_num_lt, TM_LT); break;
        case OP_LE:  compare_op_or_tm(lulu_num_le, TM_LE); break;
        case OP_ADD: arith_op_or_tm(lulu_num_add, TM_ADD); break;
        case OP_SUB: arith_op_or_tm(lulu_num_sub, TM_SUB); break;
        case OP_MUL: arith_op_or_tm(lulu_num_mul, TM_MUL); break;
        case OP_DIV: arith_op_or_tm(lulu_num_div, TM_DIV); break;
        case OP_MOD: arith_op_or_tm(lulu_num_mod, TM_MOD); break;
        case OP_POW: arith_op_or_tm(lulu_num_pow, TM_POW); break;
        case OP_CONCAT:
            // Assume at least 2 args since concat is an infix expression.
            lulu_concat(vm, read_byte());
            break;
        case OP_UNM: {
            if (is_number(arg_a))
                setv_number(arg_a, lulu_num_unm(as_number(arg_a)));
            else
                arith_tm(vm, arg_a, arg_a, TM_UNM);
            break;
        }
        case OP_NOT:
            setv_boolean(arg_a, !lulu_to_boolean(vm, -1));
            break;
        case OP_LEN:
            // TODO: Separate array segment from hash segment of tables.
            if (!is_string(arg_a))
                lulu_type_error(vm, "get length of", get_typename(arg_a));
            setv_number(arg_a, as_string(arg_a)->len);
            break;
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
        case OP_TEST:
            // Don't convert as other opcodes may need the value still.
            // Skip the OP_JUMP if truthy as it's only needed when falsy.
            if (!is_falsy(arg_a))
                vm->ip += get_opsize(OP_JUMP);
            break;
        case OP_JUMP: {
            Byte3 jump = read_byte3();
            // Sign bit is toggled?
            if (jump & MIN_SBYTE3)
                vm->ip -= jump & MAX_SBYTE3; // Clear sign bit to extract raw.
            else
                vm->ip += jump;
            break;
        }
        case OP_FORPREP: {
            StackID for_index = poke_top(vm, -3);
            StackID for_limit = poke_top(vm, -2);
            StackID for_step  = arg_a;

            if (!to_number(for_index))
                lulu_runtime_error(vm, "'for' index must be a number");
            if (!to_number(for_limit))
                lulu_runtime_error(vm, "'for' limit must be a number");
            if (!to_number(for_step))
                lulu_runtime_error(vm, "'for' step must be a number");
            if (as_number(for_step) == 0)
                lulu_runtime_error(vm, "'for' step of 0 will loop infinitely");

            // FORLOOP will increment immediately, so offset that on 1st iter.
            setv_number(for_index, lulu_num_sub(as_number(for_index),
                                                as_number(for_step)));

            // Push a copy of <for-index> to top due to parser semantics.
            lulu_push_number(vm, as_number(for_index));
            break; // Implicit OP_JUMP->OP_FORLOOP
        }
        case OP_FORLOOP: {
            Number step  = as_number(poke_top(vm, -2));
            Number limit = as_number(poke_top(vm, -3));
            Number index = lulu_num_add(as_number(arg_a), step);
            bool   pos   = lulu_num_lt(0, step);

            // Comparison we use depends on the signedness.
            if (pos ?  lulu_num_le(index, limit) : lulu_num_le(limit, index)) {
                setv_number(arg_a, index);
                setv_number(poke_top(vm, -4), index);
            } else {
                // Skip the backwards jump to loop body.
                vm->ip += get_opsize(OP_JUMP);
            }
            break;
        }
        case OP_RETURN:
            return LULU_OK;
        }
    }

#undef read_byte
#undef read_byte2
#undef read_byte3
#undef read_constant
#undef read_string
}
