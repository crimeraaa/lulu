#include <stdarg.h>
#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "limits.h"
#include "memory.h"
#include "string.h"
#include "table.h"

enum RT_ErrType {
    RTE_NEGATE,
    RTE_ARITH,
    RTE_COMPARE,
    RTE_CONCAT,
    RTE_UNDEF,   // Make sure to push the invalid variable name first.
    RTE_LENGTH,  // Using `#` on non-table and non-string values.
    RTE_INDEX,   // Using `[]` on non-table values.
};

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
    self->top = self->stack;
}

// Negative values are offset from the top, positive are offset from the base.
static Value *poke_at(VM *self, int offset)
{
    if (offset < 0) {
        return self->top + offset;
    }
    return self->stack + offset;
}

// Please avoid using this as much as possible, it's too vague!
static void push_value(VM *self, const Value *v)
{
    *self->top = *v;
    self->top += 1;
}

static void push_nils(VM *self, int n)
{
    for (int i = 0; i < n; i++) {
        setv_nil(self->top);
        self->top++;
    }
}

static void push_boolean(VM *self, bool b)
{
    setv_boolean(self->top, b);
    self->top += 1;
}

static void push_string(VM *self, String *s)
{
    setv_string(self->top, &s->object);
    self->top += 1;
}

static void runtime_error(VM *self, enum RT_ErrType rterr)
{

// Errors occur with the guilty operands at the very top of the stack.
#define _type(n)    get_typename(poke_at(self, n))
#define _cstr(n)    as_cstring(poke_at(self, n))

    size_t offset = self->ip - self->chunk->code - 1;
    int    line   = self->chunk->lines[offset];
    eprintf("%s:%i: Attempt to ", self->name, line);

    switch (rterr) {
    case RTE_NEGATE:
        eprintfln("negate a %s value", _type(-1));
        break;
    case RTE_ARITH:
        eprintfln("perform arithmetic on %s with %s", _type(-2), _type(-1));
        break;
    case RTE_COMPARE:
        eprintfln("compare %s with %s", _type(-2), _type(-1));
        break;
    case RTE_CONCAT:
        eprintfln("concatenate %s with %s", _type(-2), _type(-1));
        break;
    case RTE_UNDEF:
        eprintfln("read undefined variable '%s'.", _cstr(-1));
        break;
    case RTE_LENGTH:
        eprintfln("get length of a %s value", _type(-1));
        break;
    case RTE_INDEX:
        eprintfln("index a %s value", _type(-1));
        break;
    }
    reset_stack(self);
    longjmp(self->errorjmp, ERROR_RUNTIME);

#undef _type
#undef _cstr

}

void init_vm(VM *self, const char *name)
{
    reset_stack(self);
    init_alloc(&self->alloc, &allocfn, self);
    init_table(&self->globals);
    init_table(&self->strings);
    self->name    = name;
    self->objects = NULL;
}

void free_vm(VM *self)
{
    Alloc *alloc = &self->alloc;
    free_table(&self->globals, alloc);
    free_table(&self->strings, alloc);
    free_objects(self);
}

static String *concat_op(VM *self, int argc, Value argv[])
{
    int len = 0;
    for (int i = 0; i < argc; i++) {
        Value *arg = &argv[i];
        if (is_number(arg)) {
            char    buffer[MAX_TOSTRING];
            int     len  = num_tostring(buffer, as_number(arg));
            StrView view = make_strview(buffer, len);

            // Use `copy_string` just in case chosen representation has escapes.
            setv_string(arg, cast(Object*, copy_string(self, &view)));
        } else if (!is_string(arg)) {
            runtime_error(self, RTE_CONCAT);
        }
        len += as_string(arg)->len;
    }
    return concat_strings(self, argc, argv, len);
}

// Return `true` if in-place conversion occured without error, else `false`.
static bool converted_tonumber(Value *value)
{
    if (!is_string(value)) {
        return false;
    }
    String *string = as_string(value);
    StrView view   = make_strview(string->data, string->len);
    char   *end;
    Number  result = cstr_tonumber(view.begin, &end);

    // Don't convert yet if we need to report the error.
    if (end != view.end) {
        return false;
    }
    setv_number(value, result);
    return true;
}

typedef bool (*ValidateFn)(Value *a, Value *b);

static bool validate_arith_op(Value *a, Value *b)
{
    if (!is_number(a) && !converted_tonumber(a)) {
        return false;
    }
    if (!is_number(b) && !converted_tonumber(b)) {
        return false;
    }
    return true;
}

// By default we assume simple binary operations ONLY work for numbers.
static bool validate_binary_op(Value *a, Value *b)
{
    return is_number(a) && is_number(b);
}

static void binary_op(VM *self, OpCode op, enum RT_ErrType err, ValidateFn fn)
{
    Value *a = poke_at(self, -2);
    Value *b = poke_at(self, -1);
    if (!fn(a, b)) {
        runtime_error(self, err);
    }
    Number x = as_number(a);
    Number y = as_number(b);
    switch (op) {
    case OP_LT:  setv_boolean(a, num_lt(x, y)); break;
    case OP_LE:  setv_boolean(a, num_le(x, y)); break;
    case OP_ADD: setv_number(a, num_add(x, y)); break;
    case OP_SUB: setv_number(a, num_sub(x, y)); break;
    case OP_MUL: setv_number(a, num_mul(x, y)); break;
    case OP_DIV: setv_number(a, num_div(x, y)); break;
    case OP_MOD: setv_number(a, num_mod(x, y)); break;
    case OP_POW: setv_number(a, num_pow(x, y)); break;
    default:
        // Unreachable
        break;
    }
    // Pop 2 operands, push 1 result. We save 1 operation by modifying in-place.
    self->top -= 1;
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
#define popn(n)             (self->top -= (n))
#define pop_back()          (popn(1))

// 1}}} ------------------------------------------------------------------------

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        printf("\t");
        for (const Value *slot = self->stack; slot < self->top; slot++) {
            printf("[ ");
            print_value(slot, true);
            printf(" ]");
        }
        printf("\n");
        disassemble_instruction(chunk, cast(int, self->ip - chunk->code));
#endif
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
            popn(read_byte());
            break;
        case OP_GETLOCAL:
            push_value(self, &self->stack[read_byte()]);
            break;
        case OP_GETGLOBAL: {
            // Assume this is a string for the variable's name.
            Value *name = read_constant();
            Value  value;
            if (!get_table(&self->globals, name, &value)) {
                push_value(self, name);
                runtime_error(self, RTE_UNDEF);
            }
            push_value(self, &value);
            break;
        }
        case OP_GETTABLE: {
            Value *tbl = poke_at(self, -2);
            Value *key = poke_at(self, -1);
            Value  val;
            if (!is_table(tbl)) {
                // Push the guilty variable to the top so we can report it.
                push_value(self, tbl);
                runtime_error(self, RTE_INDEX);
            }
            if (!get_table(as_table(tbl), key, &val)) {
                setv_nil(&val);
            }
            popn(2);
            push_value(self, &val);
            break;
        }
        case OP_SETLOCAL:
            self->stack[read_byte()] = *poke_at(self, -1);
            pop_back();
            break;
        case OP_SETGLOBAL:
            // Same as `OP_GETGLOBAL`.
            set_table(&self->globals,
                      read_constant(),
                      poke_at(self, -1),
                      alloc);
            pop_back();
            break;
        case OP_SETTABLE: {
            int    t_idx  = read_byte();
            int    k_idx  = read_byte();
            int    to_pop = read_byte();
            Value *tbl    = poke_at(self, t_idx);
            Value *key    = poke_at(self, k_idx);
            Value *val    = poke_at(self, -1);

            if (!is_table(tbl)) {
                // Push the guilty variable to the top so we can report it.
                push_value(self, tbl);
                runtime_error(self, RTE_INDEX);
            }
            set_table(as_table(tbl), key, val, alloc);
            popn(to_pop);
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
            popn(count);
            break;
        }
        case OP_EQ: {
            Value *a = poke_at(self, -2);
            Value *b = poke_at(self, -1);
            setv_boolean(a, values_equal(a, b));
            pop_back();
            break;
        }
        case OP_LT:
        case OP_LE:
            binary_op(self, op, RTE_COMPARE, &validate_binary_op);
            break;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:
        case OP_POW:
            binary_op(self, op, RTE_ARITH, &validate_arith_op);
            break;
        case OP_CONCAT: {
            // Assume at least 2 args since concat is an infix expression.
            int     argc = read_byte();
            Value  *argv = poke_at(self, -argc);
            String *res  = concat_op(self, argc, argv);
            popn(argc);
            push_string(self, res);
            break;
        }
        case OP_UNM: {
            Value *arg = poke_at(self, -1);
            if (!is_number(arg) && !converted_tonumber(arg)) {
                runtime_error(self, RTE_NEGATE);
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
                runtime_error(self, RTE_LENGTH);
            }
            setv_number(arg, as_string(arg)->len);
            break;
        }
        case OP_PRINT: {
            int    argc = read_byte();
            Value *argv = poke_at(self, -argc);
            for (int i = 0; i < argc; i++) {
                print_value(&argv[i], false);
                printf("\t");
            }
            printf("\n");
            popn(argc);
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
#undef popn
#undef pop_back
}

ErrType interpret(VM *self, const char *input)
{
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
