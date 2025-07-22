#include <limits.h>     // CHAR_{MIN,MAX}

#include "lulu_auxlib.h"

static int
_string_len(lulu_VM *vm, int argc)
{
    size_t n;
    lulu_check_lstring(vm, 1, &n);
    (void)argc;
    lulu_push_number(vm, (lulu_Number)n);
    return 1;
}

/* maximum length of a format specifier. */
#define FMTSPEC_BUFSIZE     32

static int
_string_format(lulu_VM *vm, int argc)
{
    int argn; /* Index 0 is never valid, index 1 is the format string. */
    size_t n;
    lulu_Buffer b;
    const char *start, *it, *end;

    argn  = 1;
    start = lulu_check_lstring(vm, argn, &n);

    lulu_buffer_init(vm, &b);

    for (it = start, end = start + n; it < end; it++) {
        /* temporary storage for formatted items. */
        char fmt_item[256];
        char fmt_spec[FMTSPEC_BUFSIZE];
        int  written = 0;

        /* 'Flush' the string before the specifier. */
        if (*it == '%') {
            lulu_write_lstring(&b, start, (size_t)(it - start));
            it++;
        } else {
            continue;
        }

        /* first iteration: argn=1 -> argn=2 */
        argn++;

        /* first iteration: argn=2 > argc=2 -> false */
        if (argn > argc) {
            return lulu_arg_error(vm, argn, "no value");
        }

        start       = it + 1;
        fmt_spec[0] = '%';
        fmt_spec[1] = *it;
        fmt_spec[2] = '\0';
        switch (*it) {
        case '%':
            lulu_write_char(&b, '%');
            /* skip flushing the buffer */
            continue;
        case 'c': {
            int ch = (int)lulu_check_number(vm, argn);
            if (CHAR_MIN <= ch && ch <= CHAR_MAX) {
                written = sprintf(fmt_item, fmt_spec, ch);
            } else {
                sprintf(fmt_item, "unknown character '%c'", ch);
                return lulu_arg_error(vm, argn, fmt_item);
            }
            break;
        }
        case 'd': case 'D':
        case 'i': case 'I':
        case 'o': case 'O':
        case 'x': case 'X': {
            int i = (int)lulu_check_number(vm, argn);
            written = sprintf(fmt_item, fmt_spec, i);
            break;
        }
        case 'f': case 'F':
        case 'g': case 'G': {
            lulu_Number n = lulu_check_number(vm, argn);
            written = sprintf(fmt_item, fmt_spec, n);
            break;
        }
        case 's': {
            size_t      l;
            const char *s = lulu_check_lstring(vm, argn, &l);
            lulu_write_lstring(&b, s, l);
            /* skip flushing the buffer */
            continue;
        }

        default:
            sprintf(fmt_item, "unknown format specifier '%%%c'", *it);
            return lulu_arg_error(vm, argn, fmt_item);
        }

        lulu_write_lstring(&b, fmt_item, (size_t)written);
    }

    lulu_write_lstring(&b, start, (size_t)(it - start));
    lulu_finish_string(&b);
    return 1;
}

static const lulu_Register
stringlib[] = {
    {"len",     _string_len},
    {"format",  _string_format}
};

LULU_API int
lulu_open_string(lulu_VM *vm, int argc)
{
    const char *libname = lulu_to_string(vm, 1);
    (void)argc;
    lulu_set_library(vm, libname, stringlib, lulu_count_library(stringlib));
    return 1;
}
