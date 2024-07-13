#include <stdarg.h>
#include "object.h"
#include "vm.h"
#include "string.h"
#include "lexer.h"
#include "compiler.h"
#include "table.h"

// Simple allocation wrapper using the C standard library.
static void *stdc_allocator(void *ptr, size_t oldsz, size_t newsz, void *ctx)
{
    unused2(oldsz, ctx);
    if (newsz == 0) {
        free(ptr);
        return nullptr;
    }
    return realloc(ptr, newsz);
}

lulu_VM *lulu_open(void)
{
    static lulu_VM vm; // lol
    // Initialization (especially allocations) successful?
    if (luluVM_init(&vm, &stdc_allocator, nullptr))
        return &vm;
    else
        return nullptr;
}

void lulu_close(lulu_VM *vm)
{
    luluVM_free(vm);
}

// Negative values are offset from the top, positive are offset from the base.
static StackID poke_at_offset(lulu_VM *vm, int offset)
{
    if (offset >= 0)
        return poke_base(vm, offset);
    else
        return poke_top(vm, offset);
}

struct Run {
    Stream  stream;
    LString name;
    Chunk  *chunk;
};

static void do_load(lulu_VM *vm, void *ctx)
{
    struct Run     *r = ctx;
    static Lexer    ls;
    static Compiler cpl;
    String         *s = luluStr_copy(vm, r->name.string, r->name.length);

    // Assign here so VM has a non-NULL chunk to get filename of when erroring.
    vm->chunk = r->chunk;
    luluFun_init_chunk(r->chunk, s);
    luluCpl_init_compiler(&cpl, vm);
    luluLex_init(vm, &ls, &r->stream, &vm->buffer);
    luluCpl_compile(&cpl, &ls, r->chunk);

    // Prepare VM for execution.
    vm->ip = r->chunk->code;
    luluVM_execute(vm);
}

static const char *read_string(lulu_VM *vm, size_t *out, void *ctx)
{
    LString *s = ctx;
    unused(vm);
    // Was already read before?
    if (s->length == 0)
        return nullptr;
    // Mark as read.
    *out = s->length;
    s->length = 0;
    return s->string;
}

lulu_Status lulu_load(lulu_VM *vm, const char *input, size_t len, const char *name)
{
    static Chunk c;
    struct Run   r;
    lulu_Status  e;
    LString      s = lstr_from_len(input, len); // Context for `r.stream`.

    r.name  = lstr_from_len(name, strlen(name));
    r.chunk = &c;
    luluZIO_init_stream(vm, &r.stream, &read_string, &s);

    // We only have error handlers inside of `run_protected`.
    e = luluVM_run_protected(vm, &do_load, &r);
    luluFun_free_chunk(vm, r.chunk);
    return e;
}

void lulu_set_top(lulu_VM *vm, int offset)
{
    vm->top = poke_at_offset(vm, offset);
}

// TYPE RELATED FUNCTIONS ------------------------------------------------- {{{1

const char *lulu_get_typename(lulu_VM *vm, int offset)
{
    return get_typename(poke_at_offset(vm, offset));
}

// "IS" FUNCTIONS --------------------------------------------------------- {{{2

bool lulu_is_nil(lulu_VM *vm, int offset)
{
    return is_nil(poke_at_offset(vm, offset));
}

bool lulu_is_number(lulu_VM *vm, int offset)
{
    return is_number(poke_at_offset(vm, offset));
}

bool lulu_is_boolean(lulu_VM *vm, int offset)
{
    return is_boolean(poke_at_offset(vm, offset));
}

bool lulu_is_string(lulu_VM *vm, int offset)
{
    return is_string(poke_at_offset(vm, offset));
}

bool lulu_is_table(lulu_VM *vm, int offset)
{
    return is_table(poke_at_offset(vm, offset));
}

// 2}}} ------------------------------------------------------------------------

// "PUSH" FUNCTIONS ------------------------------------------------------- {{{2

void lulu_push_nil(lulu_VM *vm, int count)
{
    for (int i = 0; i < count; i++) {
        setv_nil(&vm->top[i]);
    }
    update_top(vm, count);
}

void lulu_push_boolean(lulu_VM *vm, bool b)
{
    setv_boolean(vm->top, b);
    incr_top(vm);
}

void lulu_push_number(lulu_VM *vm, lulu_Number n)
{
    setv_number(vm->top, n);
    incr_top(vm);
}

static void push_string(lulu_VM *vm, lulu_String *s)
{
    setv_string(vm->top, s);
    incr_top(vm);
}

void lulu_push_string(lulu_VM *vm, const char *s)
{
    lulu_push_lstring(vm, s, strlen(s));
}

void lulu_push_lstring(lulu_VM *vm, const char *s, size_t len)
{
    push_string(vm, luluStr_copy(vm, s, len));
}

void lulu_push_table(lulu_VM *vm, lulu_Table *t)
{
    setv_table(vm->top, t);
    incr_top(vm);
}

const char *lulu_push_vfstring(lulu_VM *vm, const char *fmt, va_list args)
{
    const char *iter = fmt;
    int         argc = 0;

#define push_numeric(T, fn)                                                    \
    do {                                                                       \
        char buf[LULU_MAX_TOSTRING];                                           \
        int  len = fn(buf, va_arg(args, T));                                   \
        lulu_push_lstring(vm, buf, len);                                       \
    } while (false)

#define push_character(ch)                                                     \
    do {                                                                       \
        char buf[] = {ch, '\0'};                                               \
        lulu_push_lstring(vm, buf, sizeof(buf));                               \
    } while (false);

    for (;;) {
        const char *spec = strchr(iter, '%');
        if (spec == nullptr)
            break;
        // Push the contents of the string before '%' unless '%' is first char.
        if (spec != fmt) {
            lulu_push_lstring(vm, iter, spec - iter);
            argc += 1;
        }
        // Move to character after '%' so we point at the specifier.
        spec += 1;
        switch (*spec) {
        case '%': push_character('%');                break;
        case 'c': push_character(va_arg(args, int));  break;
        case 'f': push_numeric(Number, lulu_num_tostring); break;
        case 'i': push_numeric(int,    lulu_int_tostring); break;
        case 'p': push_numeric(void*,  lulu_ptr_tostring); break;
        case 's': {
            const char *s = va_arg(args, char*);
            if (s != nullptr)
                lulu_push_string(vm, s);
            else
                lulu_push_literal(vm, "(null)");
            break;
        }
        default:
            // Unreachable! Assumes we never use any other specifier!
            break;
        }
        // Point to first character after the specifier.
        iter  = spec + 1;
        argc += 1;
    }
    // Still have stuff left in the format string?
    if (*iter != '\0') {
        lulu_push_string(vm, iter);
        argc += 1;
    }

    // concat will always allocate something, so try to avoid doing so if we
    // have 1 string as concatenating 1 string is a waste of memory.
    if (argc != 1)
        return lulu_concat(vm, argc);
    else
        return as_cstring(poke_at_offset(vm, -1));

#undef push_character
#undef push_numeric
}

const char *lulu_push_fstring(lulu_VM *vm, const char *fmt, ...)
{
    va_list     args;
    const char *s;
    va_start(args, fmt);
    s = lulu_push_vfstring(vm, fmt, args);
    va_end(args);
    return s;
}

// 2}}} -------------------------------------------------------------------------

// "TO" FUNCTIONS --------------------------------------------------------- {{{2

bool lulu_to_boolean(lulu_VM *vm, int offset)
{
    StackID dst = poke_at_offset(vm, offset);
    bool    b = !is_falsy(dst);
    setv_boolean(dst, b);
    return b;
}

lulu_Number lulu_to_number(lulu_VM *vm, int offset)
{
    StackID  dst  = poke_at_offset(vm, offset);
    ToNumber conv = luluVal_to_number(dst);

    // As is, `luluVal_to_number` does not do any error handling.
    if (conv.ok) {
        setv_number(dst, conv.number);
        return conv.number;
    }
    // Don't silently set `v` to nil if the user didn't ask for it.
    return 0;
}

static lulu_String *to_string(lulu_VM *vm, int offset)
{
    StackID dst = poke_at_offset(vm, offset);
    switch (get_tag(dst)) {
    case TYPE_NIL:
        lulu_push_literal(vm, "nil");
        break;
    case TYPE_BOOLEAN:
        lulu_push_string(vm, as_boolean(dst) ? "true" : "false");
        break;
    case TYPE_NUMBER:
        lulu_push_fstring(vm, "%f", as_number(dst));
        break;
    case TYPE_STRING:
        return as_string(dst);
    case TYPE_TABLE:
        lulu_push_fstring(vm, "%s: %p", get_typename(dst), as_pointer(dst));
        break;
    }
    // Do an in-place conversion based on the temporary string we just pushed.
    String *s = as_string(poke_at_offset(vm, -1));
    setv_string(dst, s);
    lulu_pop(vm, 1);
    return as_string(dst);
}

const char *lulu_to_string(lulu_VM *vm, int offset)
{
    return to_string(vm, offset)->data;
}

// 2}}} ------------------------------------------------------------------------

// 1}}} ------------------------------------------------------------------------

const char *lulu_concat(lulu_VM *vm, int count)
{
    Buffer *b    = &vm->buffer;
    StackID args = poke_at_offset(vm, -count);
    luluZIO_reset_buffer(b);
    for (int i = 0; i < count; i++) {
        StackID dst = &args[i];
        if (is_number(dst))
            to_string(vm, i - count);
        else if (!is_string(dst))
            lulu_type_error(vm, "concatenate", get_typename(dst));

        String *s = as_string(dst);
        size_t  n = b->length + s->length;
        if (n + 1 > b->capacity)
            luluZIO_resize_buffer(vm, b, n + 1);
        memcpy(&b->buffer[b->length], s->data, s->length);
        b->length += s->length;
    }
    String *s = luluStr_copy(vm, b->buffer, b->length);
    lulu_pop(vm, count);
    push_string(vm, s);
    return s->data;
}

void lulu_get_table(lulu_VM *vm, int t_offset, int k_offset)
{
    StackID t = poke_at_offset(vm, t_offset);
    StackID k = poke_at_offset(vm, k_offset);
    Value   v;
    if (!is_table(t))
        lulu_type_error(vm, "index", get_typename(t));
    if (!luluTbl_get(as_table(t), k, &v))
        setv_nil(&v);
    lulu_pop(vm, 2);
    push_back(vm, &v);
}

static void set_table(lulu_VM *vm, Value *t, Value *k, Value *v)
{
    if (!is_table(t))
        lulu_type_error(vm, "index", get_typename(t));
    if (is_nil(k))
        lulu_type_error(vm, "set", "nil index");
    luluTbl_set(vm, as_table(t), k, v);
}

void lulu_set_table(lulu_VM *vm, int t_offset, int k_offset, int to_pop)
{
    StackID t = poke_at_offset(vm, t_offset);
    StackID k = poke_at_offset(vm, k_offset);
    set_table(vm, t, k, poke_at_offset(vm, -1));
    lulu_pop(vm, to_pop);
}

static Value to_field(lulu_VM *vm, const char *s)
{
    Value v;
    setv_string(&v, luluStr_copy(vm, s, strlen(s)));
    return v;
}

void lulu_set_field(lulu_VM *vm, int offset, const char *s)
{
    Value k = to_field(vm, s);
    set_table(vm, poke_at_offset(vm, offset), &k, poke_at_offset(vm, -1));
    lulu_pop(vm, 1);
}

void lulu_set_global(lulu_VM *vm, const char *s)
{
    Value t;
    Value k = to_field(vm, s);
    setv_table(&t, &vm->globals);
    set_table(vm, &t, &k, poke_at_offset(vm, -1));
    lulu_pop(vm, 1);
}

void lulu_get_global(lulu_VM *vm, const char *cs)
{
    Value id = to_field(vm, cs);
    Value out;
    if (!luluTbl_get(&vm->globals, &id, &out))
        lulu_runtime_error(vm, "Global '%s' is undefined", as_string(&id)->data);
    push_back(vm, &out);
}

static Chunk *current_chunk(lulu_VM *vm)
{
    return vm->chunk;
}

static int current_line(lulu_VM *vm)
{
    // Current instruction is also the index into the lines array.
    size_t i = vm->ip - current_chunk(vm)->code - 1;
    return current_chunk(vm)->lines[i];
}

void lulu_comptime_error(lulu_VM *vm, int line, const char *what, const char *where)
{
    lulu_push_error_fstring(vm, line, "%s %s", what, where);
    luluVM_throw_error(vm, LULU_ERROR_COMPTIME);
}

void lulu_runtime_error(lulu_VM *vm, const char *fmt, ...)
{
    va_list args;
    int     line = current_line(vm);

    va_start(args, fmt);
    const char *msg = lulu_push_vfstring(vm, fmt, args);
    lulu_pop(vm, 1);
    va_end(args);

    lulu_push_error_fstring(vm, line, "%s", msg);
    luluVM_throw_error(vm, LULU_ERROR_RUNTIME);
}

void lulu_alloc_error(lulu_VM *vm)
{
    // Should have been interned on initialization. And if initialization failed,
    // we should have exited as soon as possible.
    lulu_push_string(vm, MEMORY_ERROR_MESSAGE);
    luluVM_throw_error(vm, LULU_ERROR_ALLOC);
}

void lulu_type_error(lulu_VM *vm, const char *act, const char *type)
{
    lulu_runtime_error(vm, "Attempt to %s a %s value", act, type);
}

void lulu_push_error_fstring(lulu_VM *vm, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    lulu_push_error_vfstring(vm, line, fmt, args);
    va_end(args);
}

void lulu_push_error_vfstring(lulu_VM *vm, int line, const char *fmt, va_list args)
{
    lulu_set_top(vm, 0); // Reset VM stack before pushing the error message.
    const char *msg = lulu_push_vfstring(vm, fmt, args);
    lulu_pop(vm, 1); // push_fstring will push a copy.
    lulu_push_fstring(vm, "%s:%i: %s", current_chunk(vm)->name->data, line, msg);
}
