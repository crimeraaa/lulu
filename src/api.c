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

static void push_string(VM *self, String *s)
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
    const char *iter = fmt;
    int         argc = 1;
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
            argc += 1;
        }
        // Move to character after '%' so we point at the specifier.
        spec += 1;
        switch (*spec) {
        case 'i':
        case 'd':
            push_number(self, va_arg(argp, int));
            break;
        case 's':
            push_cstring(self, va_arg(argp, const char*));
            break;
        case 'p': {
            char buf[MAX_TOSTRING];
            int  len = snprintf(buf, sizeof(buf), "%p", va_arg(argp, void*));
            push_lcstring(self, buf, len);
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
        argc += 1;
    }
    concat_op(self, argc, poke_at(self, -argc));
}

void push_fstring(VM *self, const char *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    push_vfstring(self, fmt, argp);
    va_end(argp);
}

void concat_op(VM *self, int argc, Value *argv)
{
    int len = 0;
    for (int i = 0; i < argc; i++) {
        Value *arg = &argv[i];
        if (is_number(arg)) {
            char    buffer[MAX_TOSTRING];
            int     len  = num_tostring(buffer, as_number(arg));
            StrView view = make_strview(buffer, len);

            // Use `copy_string` just in case chosen representation has escapes.
            setv_string(arg, cast(Object*, copy_string(self, view)));
        } else if (!is_string(arg)) {
            runtime_error(self, "concatenate", get_typename(arg));
        }
        len += as_string(arg)->len;
    }
    String *res = concat_strings(self, argc, argv, len);
    popn(self, argc);
    push_string(self, res);
}
