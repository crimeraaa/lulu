#include <stdarg.h>
#include "api.h"
#include "object.h"
#include "vm.h"
#include "string.h"
#include "table.h"

Value *poke_at(VM *self, int offset)
{
    if (offset < 0) {
        return self->top + offset;
    }
    return self->base + offset;
}

void push_nils(VM *self, int count)
{
    for (int i = 0; i < count; i++) {
        setv_nil(&self->top[i]);
    }
    update_top(self, count);
}

void push_boolean(VM *self, bool b)
{
    setv_boolean(self->top, b);
    incr_top(self);
}

void push_number(VM *self, Number n)
{
    setv_number(self->top, n);
    incr_top(self);
}

void push_string(VM *self, String *s)
{
    setv_string(self->top, s);
    incr_top(self);
}

void push_cstring(VM *self, const char *s)
{
    int     len  = strlen(s);
    StrView view = make_strview(s, len);
    push_string(self, copy_string(self, view));
}

void push_lcstring(VM *self, const char *s, int len)
{
    StrView view = make_strview(s, len);
    push_string(self, copy_string(self, view));
}

const char *push_vfstring(VM *self, const char *fmt, va_list argp)
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
            StrView view = make_strview(iter, spec - iter);
            push_lcstring(self, view.begin, view.len);
            argc  += 1;
            total += view.len;
        }
        // Move to character after '%' so we point at the specifier.
        spec += 1;
        switch (*spec) {
        case 'c': {
            char buf[2];
            buf[0] = cast(char, va_arg(argp, int)); // char promoted to int
            buf[1] = '\0';
            push_lcstring(self, buf, sizeof(buf));
            break;
        }
        case 'i': {
            char buf[MAX_TOSTRING];
            int  len = int_tostring(buf, va_arg(argp, int));
            push_lcstring(self, buf, len);
            break;
        }
        case 's': {
            const char *s = va_arg(argp, char*);
            push_cstring(self, (s != NULL) ? s : "(null)");
            break;
        }
        case 'p': {
            char buf[MAX_TOSTRING];
            int  len = ptr_tostring(buf, va_arg(argp, void*));
            push_lcstring(self, buf, len);
            break;
        }
        default:
            // Unreachable! Assumes we never use any other specifier!
            break;
        }
        // Point to first character after the specifier.
        iter   = spec + 1;
        total += as_string(poke_at(self, -1))->len;
        argc  += 1;
    }
    // Still have stuff left in the format string?
    if (*iter != '\0') {
        push_cstring(self, iter);
        argc  += 1;
        total += as_string(poke_at(self, -1))->len;
    }
    String *res = concat_strings(self, argc, poke_at(self, -argc), total);
    popn(self, argc);
    push_string(self, res);
    return res->data;
}

const char *push_fstring(VM *self, const char *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    const char *res = push_vfstring(self, fmt, argp);
    va_end(argp);
    return res;
}

const char *push_tostring(VM *self, int offset)
{
    Value *v = poke_at(self, offset);
    switch (get_tag(v)) {
    case TYPE_NIL:
        push_cstring(self, "nil");
        break;
    case TYPE_BOOLEAN:
        push_cstring(self, as_boolean(v) ? "true" : "false");
        break;
    case TYPE_NUMBER: {
        char buf[MAX_TOSTRING];
        int  len = num_tostring(buf, as_number(v));
        push_lcstring(self, buf, len);
        break;
    }
    case TYPE_STRING:
        push_string(self, as_string(v));
        break;
    case TYPE_TABLE:
        push_fstring(self, "%s: %p", get_typename(v), as_pointer(v));
        break;
    }
    return as_string(poke_at(self, -1))->data;
}
