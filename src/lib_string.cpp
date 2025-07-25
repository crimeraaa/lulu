#include <ctype.h>      /* is* */
#include <limits.h>     /* CHAR_{MIN,MAX} */
#include <string.h>     /* memchr */

#include "lulu_auxlib.h"

#define cast(T)         (T)
#define unused(expr)    (void)(expr)

static lulu_Integer
resolve_index(lulu_Integer i, lulu_Integer n)
{
    /**
     * @brief
     *      Relative string position, so resolve to back of the string.
     *      e.g. if n == 3 and i == -1
     *      then i = i + (n+1)
     *      then i = -1 + 4
     *      then i = 3
     */
    if (i < 0) {
        i += n + 1;
    }
    return (i > 0) ? i - 1 : 0;
}

static const char *
get_lstring(lulu_VM *vm, int argn, lulu_Integer *n)
{
    size_t tmp = 0;
    const char *s = lulu_check_lstring(vm, argn, &tmp);
    *n = cast(lulu_Integer)tmp;
    return s;
}

static int
string_byte(lulu_VM *vm)
{
    lulu_Integer i = 0, n = 0;
    const char  *s = get_lstring(vm, 1, &n);
    lulu_Integer start = lulu_opt_integer(vm, 2, /* def */ 1);
    lulu_Integer stop  = lulu_opt_integer(vm, 3, /* def */ start);

    start = resolve_index(start, n);
    stop  = resolve_index(stop, n);
    /* Use `<=` because `stop` is inclusive. */
    for (i = start; i <= stop; i++) {
        lulu_push_integer(vm, cast(lulu_Integer)s[i]);
    }
    return cast(int)(stop - start + 1);
}

static int
string_char(lulu_VM *vm)
{
    int argc = lulu_get_top(vm);
    int i;
    lulu_Buffer b;
    lulu_buffer_init(vm, &b);
    for (i = 1; i <= argc; i++) {
        int  n  = cast(int)lulu_to_integer(vm, i);
        char ch = cast(char)n;
        /* Roundtrip causes overflow? */
        if (cast(int)ch != n) {
            return lulu_arg_error(vm, i, "invalid character code");
        }
        lulu_write_char(&b, ch);
    }
    lulu_finish_string(&b);
    return 1;
}

static int
string_len(lulu_VM *vm)
{
    lulu_Integer n;
    get_lstring(vm, 1, &n);
    lulu_push_integer(vm, n);
    return 1;
}

static int
string_sub(lulu_VM *vm)
{
    lulu_Integer n     = 0;
    const char  *s     = get_lstring(vm, 1, &n);
    lulu_Integer start = lulu_check_integer(vm, 2);
    lulu_Integer stop  = lulu_opt_integer(vm, 3, /* def */ n);

    start = resolve_index(start, n);
    stop  = resolve_index(stop, n);

    /* clamp ranges */
    if (start <= 0) {
        start = 0;
    }
    if (stop > n) {
        stop = n;
    }
    if (start <= stop) {
        /* Add 1 to length because `s[stop]` is inclusive. */
        lulu_push_lstring(vm, s + start, stop - start + 1);
    } else {
        lulu_push_literal(vm, "");
    }
    return 1;
}

static int
string_rep(lulu_VM *vm)
{
    lulu_Buffer b;
    lulu_Integer i, j;

    size_t       n = 0;
    const char  *s = lulu_check_lstring(vm, 1, &n);
    j = lulu_check_integer(vm, 2);
    lulu_buffer_init(vm, &b);
    for (i = 0; i < j; i++) {
        lulu_write_lstring(&b, s, n);
    }
    lulu_finish_string(&b);
    return 1;
}

static int
string_lower(lulu_VM *vm)
{
    lulu_Buffer b;
    size_t      i, n = 0;
    const char *s = lulu_check_lstring(vm, 1, &n);

    lulu_buffer_init(vm, &b);
    for (i = 0; i < n; i++) {
        lulu_write_char(&b, cast(char)tolower(s[i]));
    }
    lulu_finish_string(&b);
    return 1;
}

static int
string_upper(lulu_VM *vm)
{
    lulu_Buffer b;
    size_t      i, n = 0;
    const char *s = lulu_check_lstring(vm, 1, &n);

    lulu_buffer_init(vm, &b);
    for (i = 0; i < n; i++) {
        lulu_write_char(&b, cast(char)toupper(s[i]));
    }
    lulu_finish_string(&b);
    return 1;
}

static int
string_find(lulu_VM *vm)
{
    lulu_Integer s_len = 0, p_len = 0;
    const char *s = get_lstring(vm, 1, &s_len);
    const char *p = get_lstring(vm, 2, &p_len);

    lulu_Integer start  = lulu_opt_integer(vm, 3, /* def */ 0);
    for (start = resolve_index(start, s_len); start < s_len; start++) {
        /* Pattern could not possibly fit from this point onwards? */
        if (start + p_len > s_len) {
            break;
        }

        lulu_Integer stop = start;
        for (lulu_Integer i = 0; i < p_len; i++) {
            if (s[stop] != p[i]) {
                break;
            }
            stop++;
        }

        /* Almost correct, but gives different results when `p == ""`. */
        if (stop != start) {
            lulu_push_integer(vm, start + 1);
            lulu_push_integer(vm, stop);
            return 2;
        }
    }

    lulu_push_nil(vm, 1);
    return 1;
}

/* maximum length of a format specifier. */
#define FMTSPEC_BUFSIZE     32

static int
string_format(lulu_VM *vm)
{
    int argc, argn; /* Index 0 is never valid, index 1 is the format string. */
    size_t n = 0;
    lulu_Buffer b;
    const char *start, *it, *end;

    argc  = lulu_get_top(vm);
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
            lulu_write_lstring(&b, start, cast(size_t)(it - start));
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
            int ch = cast(int)lulu_check_number(vm, argn);
            if (CHAR_MIN <= ch && ch <= CHAR_MAX) {
                written = sprintf(fmt_item, fmt_spec, ch);
            } else {
                sprintf(fmt_item, "unknown character code '%i'", ch);
                return lulu_arg_error(vm, argn, fmt_item);
            }
            break;
        }
        case 'd': case 'D':
        case 'i': case 'I':
        case 'o': case 'O':
        case 'x': case 'X': {
            int i = cast(int)lulu_check_integer(vm, argn);
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
            size_t      l = 0;
            const char *s = lulu_check_lstring(vm, argn, &l);
            lulu_write_lstring(&b, s, l);
            /* skip flushing the buffer */
            continue;
        }

        default:
            sprintf(fmt_item, "unknown format specifier '%%%c'", *it);
            return lulu_arg_error(vm, argn, fmt_item);
        }

        lulu_write_lstring(&b, fmt_item, cast(size_t)written);
    }

    lulu_write_lstring(&b, start, cast(size_t)(it - start));
    lulu_finish_string(&b);
    return 1;
}

static const lulu_Register
stringlib[] = {
    {"byte",    string_byte},
    {"char",    string_char},
    {"find",    string_find},
    {"format",  string_format},
    {"len",     string_len},
    {"lower",   string_lower},
    {"rep",     string_rep},
    {"sub",     string_sub},
    {"upper",   string_upper}
};

LULU_API int
lulu_open_string(lulu_VM *vm)
{
    const char *libname = lulu_to_string(vm, 1);
    lulu_set_library(vm, libname, stringlib, lulu_count_library(stringlib));
    return 1;
}
