#include "api.h"
#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "limits.h"
#include "memory.h"
#include "string.h"
#include "table.h"

// Assumes that `context` points to the parent `VM*` instance.
static void *allocfn(void *ptr, size_t oldsz, size_t newsz, void *context)
{
    unused(oldsz);
    VM *vm = context;
    if (newsz == 0) {
        free(ptr);
        return NULL;
    }
    void *res = realloc(ptr, newsz);
    if (res == NULL) {
        logprintln("[FATAL ERROR]: No more memory");
        longjmp(vm->errorjmp, ERROR_ALLOC);
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
    Value G = make_table(&vm->globals);
    lulu_push_cstring(vm, "_G");
    push_back(vm, &G);
    lulu_set_global(vm, poke_top(vm, -2));
    pop_back(vm); // only val was popped by set_global, so pop key as well.
}

void init_vm(VM *vm, const char *name)
{
    reset_stack(vm);
    init_alloc(&vm->allocator, &allocfn, vm);
    init_table(&vm->globals);
    init_table(&vm->strings);
    vm->name    = name;
    vm->objects = NULL;

    // This must occur AFTER the strings table and objects list are initialized.
    init_builtin(vm);
}

void free_vm(VM *vm)
{
    Alloc *al = &vm->allocator;
    free_table(&vm->globals, al);
    free_table(&vm->strings, al);
    free_objects(vm);
}

void arith_op(VM *vm, OpCode op)
{
    Value *a = poke_top(vm, -2);
    Value *b = poke_top(vm, -1);
    if (!is_number(a) && !value_tonumber(a)) {
        runtime_error(vm, "perform arithmetic on", get_typename(a));
    }
    if (!is_number(b) && !value_tonumber(b)) {
        runtime_error(vm, "perform arithmetic on", get_typename(b));
    }
    Number x = as_number(a);
    Number y = as_number(b);
    switch (op) {
    case OP_ADD: setv_number(a, num_add(x, y)); break;
    case OP_SUB: setv_number(a, num_sub(x, y)); break;
    case OP_MUL: setv_number(a, num_mul(x, y)); break;
    case OP_DIV: setv_number(a, num_div(x, y)); break;
    case OP_MOD: setv_number(a, num_mod(x, y)); break;
    case OP_POW: setv_number(a, num_pow(x, y)); break;
    default:
        // Unreachable, assumes this function is never called wrongly!
        break;
    }
    // Pop 2 operands, push 1 result. We save 1 operation by modifying in-place.
    popn_back(vm, 1);
}

static void compare_op(VM *vm, OpCode op)
{
    Value *a = poke_top(vm, -2);
    Value *b = poke_top(vm, -1);
    if (!is_number(a)) {
        runtime_error(vm, "compare", get_typename(a));
    }
    if (!is_number(b)) {
        runtime_error(vm, "compare", get_typename(b));
    }
    Number x = as_number(a);
    Number y = as_number(b);
    switch (op) {
    case OP_LT: setv_boolean(a, num_lt(x, y)); break;
    case OP_LE: setv_boolean(a, num_le(x, y)); break;
    default:
        // Unreachable
        break;
    }
    pop_back(vm);
}

static void concat_op(VM *vm, int argc, Value *argv)
{
    int len = 0;
    for (int i = 0; i < argc; i++) {
        Value *arg = &argv[i];
        if (is_number(arg)) {
            char    buf[MAX_TOSTRING];
            int     len = num_tostring(buf, as_number(arg));
            StrView sv  = make_strview(buf, len);

            // Use `copy_string` just in case chosen representation has escapes.
            setv_string(arg, copy_string(vm, sv));
        } else if (!is_string(arg)) {
            runtime_error(vm, "concatenate", get_typename(arg));
        }
        len += as_string(arg)->len;
    }
    String *s = concat_strings(vm, argc, argv, len);
    popn_back(vm, argc);
    lulu_push_string(vm, s);
}

static ErrType run(VM *vm)
{
    Alloc *al  = &vm->allocator;
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
            popn_back(vm, read_byte());
            break;
        case OP_NEWTABLE: {
            Table *t = new_table(read_byte3(), al);
            setv_table(vm->top, t);
            incr_top(vm);
            break;
        }
        case OP_GETLOCAL:
            push_back(vm, &vm->base[read_byte()]);
            break;
        case OP_GETGLOBAL:
            lulu_get_global(vm, read_constant());
            break;
        case OP_GETTABLE:
            lulu_get_table(vm, -2, -1);
            break;
        case OP_SETLOCAL:
            vm->base[read_byte()] = *poke_top(vm, -1);
            pop_back(vm);
            break;
        case OP_SETGLOBAL:
            lulu_set_global(vm, read_constant());
            break;
        case OP_SETTABLE:
            lulu_set_table(vm, read_byte(), read_byte(), read_byte());
            break;
        case OP_SETARRAY: {
            int     t_idx = read_byte(); // Absolute index of the table.
            int     count = read_byte(); // How many elements in the array?
            Table  *t     = as_table(poke_base(vm, t_idx));

            // Remember: Lua uses 1-based indexing!
            for (int i = 1; i <= count; i++) {
                Value  k = make_number(i);
                Value *v = poke_base(vm, t_idx + i);
                set_table(t, &k, v, al);
            }
            popn_back(vm, count);
            break;
        }
        case OP_EQ: {
            Value *a = poke_top(vm, -2);
            Value *b = poke_top(vm, -1);
            setv_boolean(a, values_equal(a, b));
            pop_back(vm);
            break;
        }
        case OP_LT:
        case OP_LE:
            compare_op(vm, op);
            break;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:
        case OP_POW:
            arith_op(vm, op);
            break;
        case OP_CONCAT: {
            // Assume at least 2 args since concat is an infix expression.
            int     argc = read_byte();
            Value  *argv = poke_top(vm, -argc);
            concat_op(vm, argc, argv);
            break;
        }
        case OP_UNM: {
            Value *arg = poke_top(vm, -1);
            if (!is_number(arg) && !value_tonumber(arg)) {
                runtime_error(vm, "negate", get_typename(arg));
            }
            setv_number(arg, num_unm(as_number(arg)));
            break;
        }
        case OP_NOT: {
            Value *arg = poke_top(vm, -1);
            setv_boolean(arg, is_falsy(arg));
            break;
        }
        case OP_LEN: {
            // TODO: Separate array segment from hash segment of tables.
            Value *arg = poke_top(vm, -1);
            if (!is_string(arg)) {
                runtime_error(vm, "get length of", get_typename(arg));
            }
            setv_number(arg, as_string(arg)->len);
            break;
        }
        case OP_PRINT: {
            int argc = read_byte();
            for (int i = 0; i < argc; i++) {
                printf("%s\t", lulu_tostring(vm, i - argc));
                pop_back(vm);
            }
            printf("\n");
            popn_back(vm, argc);
            break;
        }
        case OP_RETURN:
            return ERROR_NONE;
        }
    }

#undef read_byte
#undef read_byte2
#undef read_byte3
#undef read_constant
#undef read_string
}

ErrType interpret(VM *vm, const char *input)
{
    Chunk    ck;
    Lexer    ls;
    Compiler cpl;
    ErrType  err = setjmp(vm->errorjmp); // Assumes always called correctly!
    switch (err) {
    case ERROR_NONE:
        init_chunk(&ck, vm->name);
        init_compiler(&cpl, &ls, vm);
        compile(&cpl, input, &ck);
        break;
    case ERROR_COMPTIME: // Fall through
    case ERROR_RUNTIME:
    case ERROR_ALLOC:
        // For the default case, please ensure all calls of `longjmp` ONLY
        // ever pass an `ErrType` member.
        free_chunk(&ck, &vm->allocator);
        return err;
    }
    // Prep the VM
    vm->chunk = &ck;
    vm->ip    = ck.code;
    err       = run(vm);
    free_chunk(&ck, &vm->allocator);
    return err;
}

void runtime_error(VM *vm, const char *act, const char *type)
{
    size_t offset = vm->ip - vm->chunk->code - 1;
    int    line   = vm->chunk->lines[offset];
    lulu_push_fstring(vm, "%s:%i: Attempt to %s a %s value\n",
                      vm->name,
                      line,
                      act,
                      type);
    print_value(poke_top(vm, -1), false);
    reset_stack(vm);
    longjmp(vm->errorjmp, ERROR_RUNTIME);
}
