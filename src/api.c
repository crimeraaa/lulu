#include <stdarg.h>
#include "api.h"
#include "object.h"
#include "vm.h"
#include "string.h"
#include "table.h"

// Negative values are offset from the top, positive are offset from the base.
static Value *poke_at_offset(VM *vm, int offset)
{
    return (offset >= 0) ? poke_base(vm, offset) : poke_top(vm, offset);
}

void lulu_set_top(lulu_VM *vm, int offset)
{
    if (offset >= 0)
        vm->top = vm->base + offset;
    else
        vm->top = vm->top + offset;
}

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

void lulu_push_string(lulu_VM *vm, lulu_String *s)
{
    setv_string(vm->top, s);
    incr_top(vm);
}

void lulu_push_cstring(lulu_VM *vm, const char *s)
{
    lulu_push_lcstring(vm, s, strlen(s));
}

void lulu_push_lcstring(lulu_VM *vm, const char *s, int len)
{
    StringView sv = sv_create_from_len(s, len);
    lulu_push_string(vm, copy_string(vm, sv));
}

void lulu_push_table(lulu_VM *vm, lulu_Table *t)
{
    setv_table(vm->top, t);
    incr_top(vm);
}

const char *lulu_push_vfstring(lulu_VM *vm, const char *fmt, va_list ap)
{
    const char  *iter  = fmt;
    int          argc  = 0;
    int          total = 0;

    for (;;) {
        const char *spec = strchr(iter, '%');
        if (spec == NULL)
            break;
        // Push the contents of the string before '%', except if '%' is starter.
        if (spec != fmt) {
            StringView sv = sv_create_from_end(iter, spec);
            lulu_push_lcstring(vm, sv.begin, sv.len);
            argc  += 1;
            total += sv.len;
        }
        // Move to character after '%' so we point at the specifier.
        spec += 1;
        switch (*spec) {
        case '%':
        case 'c': {
            char buf[2];
            if (*spec == '%')
                buf[0] = '%';
            else
                buf[0] = cast(char, va_arg(ap, int));
            buf[1] = '\0';
            lulu_push_cstring(vm, buf);
            break;
        }
        case 'f':
        case 'i':
        case 'p': {
            char buf[MAX_TOSTRING];
            int  len;
            if (*spec == 'f')
                len = num_tostring(buf, va_arg(ap, lulu_Number));
            else if (*spec == 'i')
                len = int_tostring(buf, va_arg(ap, int));
            else
                len = ptr_tostring(buf, va_arg(ap, void*));
            lulu_push_lcstring(vm, buf, len);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, char*);
            if (s != NULL)
                lulu_push_cstring(vm, s);
            else
                lulu_push_literal(vm, "(null)");
            break;
        }
        default:
            // Unreachable! Assumes we never use any other specifier!
            break;
        }
        // Point to first character after the specifier.
        iter   = spec + 1;
        total += as_string(poke_at_offset(vm, -1))->len;
        argc  += 1;
    }
    // Still have stuff left in the format string?
    if (*iter != '\0') {
        lulu_push_cstring(vm, iter);
        argc  += 1;
        total += as_string(poke_at_offset(vm, -1))->len;
    }

    // concat will always allocate something, so try to avoid doing so if we
    // have 1 string as concatenating 1 string is a waste of memory.
    if (argc != 1) {
        Value  *argv = poke_at_offset(vm, -argc);
        String *s    = concat_strings(vm, argc, argv, total);
        lulu_pop(vm, argc);
        lulu_push_string(vm, s);
        return s->data;
    }
    return as_cstring(poke_at_offset(vm, -1));
}

const char *lulu_push_fstring(lulu_VM *vm, const char *fmt, ...)
{
    va_list     ap;
    const char *s;
    va_start(ap, fmt);
    s = lulu_push_vfstring(vm, fmt, ap);
    va_end(ap);
    return s;
}

bool lulu_to_boolean(lulu_VM *vm, int offset)
{
    Value *v = poke_at_offset(vm, offset);
    bool   b = is_falsy(v);
    setv_boolean(v, b);
    return b;
}

lulu_Number lulu_to_number(lulu_VM *vm, int offset)
{
    Value *v = poke_at_offset(vm, offset);
    Value  tmp;
    // As is, this function does not do any error handling.
    if (value_tonumber(v, &tmp)) {
        Number n = as_number(&tmp);
        setv_number(v, n);
        return n;
    }
    // Don't silently set `v` to nil if the user didn't ask for it.
    return 0;
}

lulu_String *lulu_to_string(lulu_VM *vm, int offset)
{
    Value *vl = poke_at_offset(vm, offset);
    switch (get_tag(vl)) {
    case TYPE_NIL:
        lulu_push_literal(vm, "nil");
        break;
    case TYPE_BOOLEAN:
        lulu_push_cstring(vm, as_boolean(vl) ? "true" : "false");
        break;
    case TYPE_NUMBER:
        lulu_push_fstring(vm, "%f", as_number(vl));
        break;
    case TYPE_STRING:
        lulu_push_string(vm, as_string(vl));
        break;
    case TYPE_TABLE:
        lulu_push_fstring(vm, "%s: %p", get_typename(vl), as_pointer(vl));
        break;
    }
    // Do an in-place conversion based on the temporary string we just pushed.
    String *s = as_string(poke_at_offset(vm, -1));
    setv_string(vl, s);
    lulu_pop(vm, 1);
    return as_string(vl);
}

const char *lulu_to_cstring(lulu_VM *vm, int offset)
{
    return lulu_to_string(vm, offset)->data;
}

const char *lulu_concat(lulu_VM *vm, int count)
{
    int    len  = 0;
    Value *argv = poke_at_offset(vm, -count);
    for (int i = 0; i < count; i++) {
        Value *arg = &argv[i];
        if (is_number(arg))
            lulu_to_string(vm, i - count);
        else if (!is_string(arg))
            lulu_type_error(vm, "concatenate", get_typename(arg));
        len += as_string(arg)->len;
    }
    String *s = concat_strings(vm, count, argv, len);
    lulu_pop(vm, count);
    lulu_push_string(vm, s);
    return s->data;
}

void lulu_get_table(lulu_VM *vm, int t_offset, int k_offset)
{
    Value *t = poke_at_offset(vm, t_offset);
    Value *k = poke_at_offset(vm, k_offset);
    Value  v;
    if (!is_table(t))
        lulu_type_error(vm, "index", get_typename(t));
    if (!get_table(as_table(t), k, &v))
        setv_nil(&v);
    lulu_pop(vm, 2);
    push_back(vm, &v);
}

void lulu_set_table(lulu_VM *vm, int t_offset, int k_offset, int to_pop)
{
    Value *t = poke_at_offset(vm, t_offset);
    Value *k = poke_at_offset(vm, k_offset);
    Value *v = poke_at_offset(vm, -1);
    if (!is_table(t))
        lulu_type_error(vm, "index", get_typename(t));
    if (is_nil(k))
        lulu_runtime_error(vm, "set a nil index");
    set_table(as_table(t), k, v, &vm->allocator);
    lulu_pop(vm, to_pop);
}

void lulu_get_global(lulu_VM *vm, lulu_String *s)
{
    Value id = make_string(s);
    Value out;
    if (!get_table(&vm->globals, &id, &out))
        lulu_runtime_error(vm, "read undefined global '%s'", s->data);
    push_back(vm, &out);
}

void lulu_set_global(lulu_VM *vm, lulu_String *s)
{
    Value id = make_string(s);
    set_table(&vm->globals, &id, poke_at_offset(vm, -1), &vm->allocator);
    lulu_pop(vm, 1);
}

static int get_current_line(const lulu_VM *vm)
{
    // Current instruction is also the index into the lines array.
    size_t inst = vm->ip - vm->chunk->code - 1;
    int    line = vm->chunk->lines[inst];
    return line;
}

void lulu_comptime_error(lulu_VM *vm, int line, const char *what, const char *where)
{
    lulu_push_error_fstring(vm, line, "%s %s\n", what, where);
    longjmp(vm->errorjmp, ERROR_COMPTIME);
}

void lulu_runtime_error(lulu_VM *vm, const char *fmt, ...)
{
    va_list args;
    int     line = get_current_line(vm);
    va_start(args, fmt);
    lulu_push_vfstring(vm, fmt, args);
    va_end(args);
    lulu_push_error_fstring(vm, line, "Attempt to %s\n", lulu_to_cstring(vm, -1));
    longjmp(vm->errorjmp, ERROR_RUNTIME);
}

void lulu_type_error(lulu_VM *vm, const char *act, const char *type)
{
    lulu_runtime_error(vm, "%s a %s value", act, type);
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
    lulu_push_fstring(vm, "%s:%i: ", vm->name, line);
    lulu_push_vfstring(vm, fmt, args);
    lulu_concat(vm, 2);
}
