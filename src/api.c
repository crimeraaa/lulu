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


void push_nils(VM *self, int n)
{
    for (int i = 0; i < n; i++) {
        setv_nil(&self->top[i]);
    }
    update_top(self, n);
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
    setv_string(self->top, &s->object);
    incr_top(self);
}

void push_cstring(VM *self, const char *s)
{
    push_string(self, copy_string(self, make_strview(s, strlen(s))));
}

void push_lcstring(VM *self, const char *s, int len)
{
    push_string(self, copy_string(self, make_strview(s, len)));
}

void push_vfstring(VM *self, const char *fmt, va_list argp)
{
    const char *iter  = fmt;
    int         argc  = 1;
    int         total = 0;

    // Concat strings works properly only with 2+ strings, so need a dummy.
    push_cstring(self, "");
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
        case 'i':
        case 'd': {
            char buf[MAX_TOSTRING];
            int  len = int_tostring(buf, va_arg(argp, int));
            push_lcstring(self, buf, len);
            total += len;
            break;
        }
        case 's':
            push_cstring(self, va_arg(argp, const char*));
            total += as_string(poke_at(self, -1))->len;
            break;
        case 'p': {
            char buf[MAX_TOSTRING];
            int  len = ptr_tostring(buf, va_arg(argp, void*));
            push_lcstring(self, buf, len);
            total += len;
            break;
        }
        default:
            eprintfln("Unknown format specifier '%c'.", *spec);
            break;
        }
        // Point to first character after the specifier.
        iter  = spec + 1;
        argc += 1;
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
}

void push_fstring(VM *self, const char *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    push_vfstring(self, fmt, argp);
    va_end(argp);
}
