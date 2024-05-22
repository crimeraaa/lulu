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
    int     len = strlen(s);
    StrView sv  = make_strview(s, len);
    lulu_push_string(vm, copy_string(vm, sv));
}

void lulu_push_lcstring(lulu_VM *vm, const char *s, int len)
{
    StrView sv = make_strview(s, len);
    lulu_push_string(vm, copy_string(vm, sv));
}

void lulu_push_table(lulu_VM *vm, lulu_Table *t)
{
    setv_table(vm->top, t);
    incr_top(vm);
}

const char *lulu_push_vfstring(lulu_VM *vm, const char *fmt, va_list ap)
{
    const char *iter  = fmt;
    int         argc  = 0;
    int         total = 0;

    for (;;) {
        const char *spec = strchr(iter, '%');
        if (spec == NULL) {
            break;
        }
        // Push the contents of the string before '%', except if '%' is starter.
        if (spec != fmt) {
            StrView sv = make_strview(iter, spec - iter);
            lulu_push_lcstring(vm, sv.begin, sv.len);
            argc  += 1;
            total += sv.len;
        }
        // Move to character after '%' so we point at the specifier.
        spec += 1;
        switch (*spec) {
        case 'c': {
            char buf[2];
            buf[0] = cast(char, va_arg(ap, int)); // char promoted to int
            buf[1] = '\0';
            lulu_push_lcstring(vm, buf, sizeof(buf));
            break;
        }
        case 'i': {
            char buf[MAX_TOSTRING];
            int  len = int_tostring(buf, va_arg(ap, int));
            lulu_push_lcstring(vm, buf, len);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, char*);
            lulu_push_cstring(vm, (s != NULL) ? s : "(null)");
            break;
        }
        case 'p': {
            char buf[MAX_TOSTRING];
            int  len = ptr_tostring(buf, va_arg(ap, void*));
            lulu_push_lcstring(vm, buf, len);
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
    String *res = concat_strings(vm, argc, poke_at_offset(vm, -argc), total);
    popn_back(vm, argc);
    lulu_push_string(vm, res);
    return res->data;
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

const char *lulu_tostring(lulu_VM *vm, int offset)
{
    Value *vl = poke_at_offset(vm, offset);
    switch (get_tag(vl)) {
    case TYPE_NIL:
        lulu_push_literal(vm, "nil");
        break;
    case TYPE_BOOLEAN:
        if (as_boolean(vl)) {
            lulu_push_literal(vm, "true");
        } else {
            lulu_push_literal(vm, "false");
        }
        break;
    case TYPE_NUMBER: {
        char buf[MAX_TOSTRING];
        int  len = num_tostring(buf, as_number(vl));
        lulu_push_lcstring(vm, buf, len);
        break;
    }
    case TYPE_STRING:
        lulu_push_string(vm, as_string(vl));
        break;
    case TYPE_TABLE:
        lulu_push_fstring(vm, "%s: %p", get_typename(vl), as_pointer(vl));
        break;
    }
    // Do an in-place conversion based on the temporary string we just pushed.
    setv_string(vl, as_string(poke_at_offset(vm, -1)));
    pop_back(vm);
    return as_cstring(vl);
}

const char *lulu_concat(lulu_VM *vm, int count)
{
    int    len  = 0;
    Value *argv = poke_top(vm, -count);
    for (int i = 0; i < count; i++) {
        Value *arg = &argv[i];
        if (is_number(arg)) {
            char    buf[MAX_TOSTRING];
            int     len = num_tostring(buf, as_number(arg));
            StrView sv  = make_strview(buf, len);

            // Use `copy_string` just in case chosen representation has escapes.
            setv_string(arg, copy_string(vm, sv));
        } else if (!is_string(arg)) {
            lulu_type_error(vm, "concatenate", get_typename(arg));
        }
        len += as_string(arg)->len;
    }
    String *s = concat_strings(vm, count, argv, len);
    popn_back(vm, count);
    lulu_push_string(vm, s);
    return s->data;
}

void lulu_get_table(lulu_VM *vm, int t_offset, int k_offset)
{
    Value *t = poke_at_offset(vm, t_offset);
    Value *k = poke_at_offset(vm, k_offset);
    Value  v;
    if (!is_table(t)) {
        lulu_type_error(vm, "index", get_typename(t));
    }
    if (!get_table(as_table(t), k, &v)) {
        setv_nil(&v);
    }
    popn_back(vm, 2);
    push_back(vm, &v);
}

void lulu_set_table(lulu_VM *vm, int t_offset, int k_offset, int to_pop)
{
    Value *t = poke_at_offset(vm, t_offset);
    Value *k = poke_at_offset(vm, k_offset);
    Value *v = poke_at_offset(vm, -1);
    if (!is_table(t)) {
        lulu_type_error(vm, "index", get_typename(t));
    }
    set_table(as_table(t), k, v, &vm->allocator);
    popn_back(vm, to_pop);
}

void lulu_get_global(lulu_VM *vm, const lulu_Value *k)
{
    Value out;
    if (!get_table(&vm->globals, k, &out)) {
        lulu_type_error(vm, "read", "undefined");
    }
    push_back(vm, &out);
}

void lulu_set_global(lulu_VM *vm, const lulu_Value *k)
{
    set_table(&vm->globals, k, poke_top(vm, -1), &vm->allocator);
    pop_back(vm);
}

void lulu_type_error(lulu_VM *vm, const char *act, const char *type)
{
    size_t offset = vm->ip - vm->chunk->code - 1;
    int    line   = vm->chunk->lines[offset];
    vm->base      = vm->stack; // Reset stack before pushing the error message.
    vm->top       = vm->stack;
    lulu_push_fstring(vm, "%s:%i: Attempt to %s a %s value\n",
                      vm->name,
                      line,
                      act,
                      type);
    longjmp(vm->errorjmp, ERROR_RUNTIME);
}
