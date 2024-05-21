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

static void reset_stack(VM *self)
{
    self->top  = self->stack;
    self->base = self->stack;
}

// Please avoid using this as much as possible, it works but it's vague!
static void push_value(VM *self, const Value *v)
{
    *self->top = *v;
    incr_top(self);
}

static void init_builtin(VM *self)
{
    Table *globals = &self->globals;
    Value  wrapper = make_table(globals);
    push_cstring(self, "_G");
    set_table(globals, poke_at(self, -1), &wrapper, &self->alloc);
    pop_back(self);
}

void init_vm(VM *self, const char *name)
{
    reset_stack(self);
    init_alloc(&self->alloc, &allocfn, self);
    init_table(&self->globals);
    init_table(&self->strings);
    self->name    = name;
    self->objects = NULL;

    // This must occur AFTER the strings table and objects list are initialized.
    init_builtin(self);
}

void free_vm(VM *self)
{
    Alloc *alloc = &self->alloc;
    free_table(&self->globals, alloc);
    free_table(&self->strings, alloc);
    free_objects(self);
}

void arith_op(VM *self, OpCode op)
{
    Value *a = poke_at(self, -2);
    Value *b = poke_at(self, -1);
    if (!is_number(a) && !value_tonumber(a)) {
        runtime_error(self, "perform arithmetic on", get_typename(a));
    }
    if (!is_number(b) && !value_tonumber(b)) {
        runtime_error(self, "perform arithmetic on", get_typename(b));
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
    popn(self, 1);
}

static void compare_op(VM *self, OpCode op)
{
    Value *a = poke_at(self, -2);
    Value *b = poke_at(self, -1);
    if (!is_number(a)) {
        runtime_error(self, "compare", get_typename(a));
    }
    if (!is_number(b)) {
        runtime_error(self, "compare", get_typename(b));
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
    popn(self, 1);
}

static void concat_op(VM *self, int argc, Value *argv)
{
    int len = 0;
    for (int i = 0; i < argc; i++) {
        Value *arg = &argv[i];
        if (is_number(arg)) {
            char    buffer[MAX_TOSTRING];
            int     len  = num_tostring(buffer, as_number(arg));
            StrView view = make_strview(buffer, len);

            // Use `copy_string` just in case chosen representation has escapes.
            setv_string(arg, copy_string(self, view));
        } else if (!is_string(arg)) {
            runtime_error(self, "concatenate", get_typename(arg));
        }
        len += as_string(arg)->len;
    }
    String *res = concat_strings(self, argc, argv, len);
    popn(self, argc);
    push_string(self, res);
}

static ErrType run(VM *self)
{
    Alloc *alloc     = &self->alloc;
    Chunk *chunk     = self->chunk;
    Value *constants = chunk->constants.values;

// --- HELPER MACROS ------------------------------------------------------ {{{1
// Many of these rely on variables local to this function.

#define read_byte()         (*self->ip++)

// Assumes MSB is read first then LSB.
#define read_byte2()        (decode_byte2(read_byte(), read_byte()))

// Assumes MSB is read first, then middle, then LSB.
#define read_byte3()        (encode_byte3(read_byte(), read_byte(), read_byte()))

// Assumes a 3-byte operand comes right after the opcode.
#define read_constant()     (&constants[read_byte3()])
#define read_string()       as_string(read_constant())

// 1}}} ------------------------------------------------------------------------

    for (;;) {
        if (is_enabled(DEBUG_TRACE_EXECUTION)) {
            printf("\t");
            for (const Value *slot = self->stack; slot < self->top; slot++) {
                printf("[ ");
                print_value(slot, true);
                printf(" ]");
            }
            printf("\n");
            disassemble_instruction(chunk, cast(int, self->ip - chunk->code));
        }
        OpCode op = read_byte();
        switch (op) {
        case OP_CONSTANT:
            push_value(self, read_constant());
            break;
        case OP_NIL:
            push_nils(self, read_byte());
            break;
        case OP_TRUE:
            push_boolean(self, true);
            break;
        case OP_FALSE:
            push_boolean(self, false);
            break;
        case OP_POP:
            popn(self, read_byte());
            break;
        case OP_NEWTABLE: {
            Table *tbl = new_table(alloc, read_byte3());
            setv_table(self->top, tbl);
            incr_top(self);
            break;
        }
        case OP_GETLOCAL:
            push_value(self, &self->base[read_byte()]);
            break;
        case OP_GETGLOBAL: {
            // Assume this is a string for the variable's name.
            Value *name = read_constant();
            Value  value;
            if (!get_table(&self->globals, name, &value)) {
                runtime_error(self, "read", "undefined");
            }
            push_value(self, &value);
            break;
        }
        case OP_GETTABLE: {
            Value *tbl = poke_at(self, -2);
            Value *key = poke_at(self, -1);
            Value  val;
            if (!is_table(tbl)) {
                runtime_error(self, "index", get_typename(tbl));
            }
            if (!get_table(as_table(tbl), key, &val)) {
                setv_nil(&val);
            }
            popn(self, 2);
            push_value(self, &val);
            break;
        }
        case OP_SETLOCAL:
            self->base[read_byte()] = *poke_at(self, -1);
            pop_back(self);
            break;
        case OP_SETGLOBAL:
            // Same as `OP_GETGLOBAL`.
            set_table(&self->globals,
                      read_constant(),
                      poke_at(self, -1),
                      alloc);
            pop_back(self);
            break;
        case OP_SETTABLE: {
            int    t_idx  = read_byte(); // Absolute index of the table.
            int    k_idx  = read_byte(); // Absolute index of the key.
            int    to_pop = read_byte(); // How many elements to pop?
            Value *tbl    = poke_at(self, t_idx);
            Value *key    = poke_at(self, k_idx);
            Value *val    = poke_at(self, -1);

            if (!is_table(tbl)) {
                runtime_error(self, "index", get_typename(tbl));
            }
            set_table(as_table(tbl), key, val, alloc);
            popn(self, to_pop);
            break;
        }
        case OP_SETARRAY: {
            int     t_idx = read_byte(); // Absolute index of the table.
            int     count = read_byte(); // How many elements in the array?
            Table  *tbl   = as_table(poke_at(self, t_idx)); // Assume correct!

            // Remember: Lua uses 1-based indexing!
            for (int i = 1; i <= count; i++) {
                Value  key = make_number(i);
                Value *val = poke_at(self, t_idx + i);
                set_table(tbl, &key, val, alloc);
            }
            popn(self, count);
            break;
        }
        case OP_EQ: {
            Value *a = poke_at(self, -2);
            Value *b = poke_at(self, -1);
            setv_boolean(a, values_equal(a, b));
            pop_back(self);
            break;
        }
        case OP_LT:
        case OP_LE:
            compare_op(self, op);
            break;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:
        case OP_POW:
            arith_op(self, op);
            break;
        case OP_CONCAT: {
            // Assume at least 2 args since concat is an infix expression.
            int     argc = read_byte();
            Value  *argv = poke_at(self, -argc);
            concat_op(self, argc, argv);
            break;
        }
        case OP_UNM: {
            Value *arg = poke_at(self, -1);
            if (!is_number(arg) && !value_tonumber(arg)) {
                runtime_error(self, "negate", get_typename(arg));
            }
            setv_number(arg, num_unm(as_number(arg)));
            break;
        }
        case OP_NOT: {
            Value *arg = poke_at(self, -1);
            setv_boolean(arg, is_falsy(arg));
            break;
        }
        case OP_LEN: {
            // TODO: Add support for tables, will need to implement arrays then
            Value *arg = poke_at(self, -1);
            if (!is_string(arg)) {
                runtime_error(self, "get length of", get_typename(arg));
            }
            setv_number(arg, as_string(arg)->len);
            break;
        }
        case OP_PRINT: {
            int argc = read_byte();
            for (int i = 0; i < argc; i++) {
                printf("%s\t", push_tostring(self, i - argc));
                pop_back(self);
            }
            printf("\n");
            popn(self, argc);
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

ErrType interpret(VM *self, const char *input)
{
    Chunk    chunk;
    Lexer    lexer;
    Compiler compiler;
    ErrType  err = setjmp(self->errorjmp); // Assumes always called correctly!
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

void runtime_error(VM *self, const char *act, const char *type)
{
    size_t offset = self->ip - self->chunk->code - 1;
    int    line   = self->chunk->lines[offset];
    push_fstring(self, "%s:%i: Attempt to %s a %s value\n",
                self->name,
                line,
                act,
                type);
    print_value(poke_at(self, -1), false);
    reset_stack(self);
    longjmp(self->errorjmp, ERROR_RUNTIME);
}
