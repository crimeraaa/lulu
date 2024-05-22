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

void lulu_push_nil(struct lulu_VM *vm, int count)
{
    for (int i = 0; i < count; i++) {
        setv_nil(&vm->top[i]);
    }
    update_top(vm, count);
}

void lulu_push_boolean(struct lulu_VM *vm, bool b)
{
    setv_boolean(vm->top, b);
    incr_top(vm);
}

void lulu_push_number(struct lulu_VM *vm, lulu_Number n)
{
    setv_number(vm->top, n);
    incr_top(vm);
}

void lulu_push_string(struct lulu_VM *vm, struct lulu_String *s)
{
    setv_string(vm->top, s);
    incr_top(vm);
}

void lulu_push_cstring(struct lulu_VM *vm, const char *s)
{
    int     len = strlen(s);
    StrView sv  = make_strview(s, len);
    lulu_push_string(vm, copy_string(vm, sv));
}

void lulu_push_lcstring(struct lulu_VM *vm, const char *s, int len)
{
    StrView sv = make_strview(s, len);
    lulu_push_string(vm, copy_string(vm, sv));
}

void lulu_push_table(struct lulu_VM *vm, struct lulu_Table *t)
{
    setv_table(vm->top, t);
    incr_top(vm);
}

const char *lulu_push_vfstring(struct lulu_VM *vm, const char *fmt, va_list argp)
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
            buf[0] = cast(char, va_arg(argp, int)); // char promoted to int
            buf[1] = '\0';
            lulu_push_lcstring(vm, buf, sizeof(buf));
            break;
        }
        case 'i': {
            char buf[MAX_TOSTRING];
            int  len = int_tostring(buf, va_arg(argp, int));
            lulu_push_lcstring(vm, buf, len);
            break;
        }
        case 's': {
            const char *s = va_arg(argp, char*);
            lulu_push_cstring(vm, (s != NULL) ? s : "(null)");
            break;
        }
        case 'p': {
            char buf[MAX_TOSTRING];
            int  len = ptr_tostring(buf, va_arg(argp, void*));
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

const char *lulu_push_fstring(struct lulu_VM *vm, const char *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    const char *res = lulu_push_vfstring(vm, fmt, argp);
    va_end(argp);
    return res;
}

const char *lulu_tostring(struct lulu_VM *vm, int offset)
{
    Value *v = poke_at_offset(vm, offset);
    switch (get_tag(v)) {
    case TYPE_NIL:
        lulu_push_cstring(vm, "nil");
        break;
    case TYPE_BOOLEAN:
        lulu_push_cstring(vm, as_boolean(v) ? "true" : "false");
        break;
    case TYPE_NUMBER: {
        char buf[MAX_TOSTRING];
        int  len = num_tostring(buf, as_number(v));
        lulu_push_lcstring(vm, buf, len);
        break;
    }
    case TYPE_STRING:
        lulu_push_string(vm, as_string(v));
        break;
    case TYPE_TABLE:
        lulu_push_fstring(vm, "%s: %p", get_typename(v), as_pointer(v));
        break;
    }
    return as_string(poke_at_offset(vm, -1))->data;
}

void lulu_get_table(struct lulu_VM *vm, int t_offset, int k_offset)
{
    Value *t = poke_at_offset(vm, t_offset);
    Value *k = poke_at_offset(vm, k_offset);
    Value  v;
    if (!is_table(t)) {
        runtime_error(vm, "index", get_typename(t));
    }
    if (!get_table(as_table(t), k, &v)) {
        setv_nil(&v);
    }
    popn_back(vm, 2);
    push_back(vm, &v);
}

void lulu_set_table(struct lulu_VM *vm, int t_offset, int k_offset, int to_pop)
{
    Value *t = poke_at_offset(vm, t_offset);
    Value *k = poke_at_offset(vm, k_offset);
    Value *v = poke_at_offset(vm, -1);
    if (!is_table(t)) {
        runtime_error(vm, "index", get_typename(t));
    }
    set_table(as_table(t), k, v, &vm->alloc);
    popn_back(vm, to_pop);
}

void lulu_get_global(struct lulu_VM *vm, const struct lulu_Value *k)
{
    Value out;
    if (!get_table(&vm->globals, k, &out)) {
        runtime_error(vm, "read", "undefined");
    }
    push_back(vm, &out);
}

void lulu_set_global(struct lulu_VM *vm, const struct lulu_Value *k)
{
    set_table(&vm->globals, k, poke_top(vm, -1), &vm->alloc);
    pop_back(vm);
}
