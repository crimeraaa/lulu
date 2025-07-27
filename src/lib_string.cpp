#include <ctype.h>      /* is* */
#include <limits.h>     /* CHAR_{MIN,MAX} */
#include <string.h>     /* memchr */

#include "lulu_auxlib.h"

#define cast(T)         (T)
#define unused(expr)    (void)(expr)

typedef unsigned char uchar;

static size_t
resolve_index(lulu_Integer i, size_t n)
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
    return cast(size_t)((i > 0) ? i - 1 : 0);
}

static int
string_byte(lulu_VM *vm)
{
    size_t i = 0, n = 0;
    const char *s = lulu_check_lstring(vm, 1, &n);
    size_t start = resolve_index(lulu_opt_integer(vm, 2, /* def */ 1), n);
    size_t stop  = resolve_index(lulu_opt_integer(vm, 3, /* def */ start), n);
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
        int   n  = cast(int)lulu_to_integer(vm, i);
        uchar ch = cast(uchar)n;
        /* Roundtrip causes overflow? */
        if (cast(int)ch != n) {
            char buf[128];
            sprintf(buf, "Invalid character code '%i'", n);
            return lulu_arg_error(vm, i, buf);
        }
        lulu_write_char(&b, cast(char)ch);
    }
    lulu_finish_string(&b);
    return 1;
}

static int
string_len(lulu_VM *vm)
{
    size_t n;
    lulu_check_lstring(vm, 1, &n);
    lulu_push_integer(vm, cast(lulu_Integer)n);
    return 1;
}

static int
string_sub(lulu_VM *vm)
{
    size_t       n = 0;
    const char  *s = lulu_check_lstring(vm, 1, &n);
    size_t start = resolve_index(lulu_check_integer(vm, 2), n);
    size_t stop  = resolve_index(lulu_opt_integer(vm, 3, /* def */ n), n);
    /* clamp ranges */
    if (start >= n) {
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
    size_t s_len = 0, p_len = 0;
    const char *s = lulu_check_lstring(vm, 1, &s_len);
    const char *p = lulu_check_lstring(vm, 2, &p_len);

    size_t start = resolve_index(lulu_opt_integer(vm, 3, /* def */ 0), s_len);
    for (; start < s_len; start++) {
        /* Pattern could not possibly fit from this point onwards? */
        if (start + p_len > s_len) {
            break;
        }

        size_t stop = start;
        for (size_t i = 0; i < p_len; i++) {
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


/* man 3 printf: Flag characters */
#define FMT_FLAGS       "#0- +"

/* length of a format string for a single item, plus some legroom. */
#define FMT_BUFSIZE     (sizeof(FMT_FLAGS) + 10)


/**
 * @brief
 *      Parses the format string starting at `fmt_str` and writes it into
 *      `fmt_buf`.
 *
 * @note(2025-07-27)
 * Assumptions:
 *
 *      1.) `fmt_str[0]` is the character AFTER the `%` character.
 *
 * @return
 *      The pointer to the format specifier proper.
 */
static const char *
get_format(lulu_VM *vm, const char *fmt_str, char (&fmt_buf)[FMT_BUFSIZE])
{
    size_t i = 0;
    fmt_buf[0] = '%';
    while (fmt_str[i] != '\0' && strchr(FMT_FLAGS, fmt_str[i]) != NULL) {
        i++;
    }

    /* Repeated 1 or more flags? */
    if (i >= sizeof(FMT_FLAGS)) {
        lulu_errorf(vm, "invalid format (repeated flags)");
    }

    /* Width: 2 digits at most */
    if (isdigit(cast(uchar)fmt_str[i])) {
        i++;
    }
    if (isdigit(cast(uchar)fmt_str[i])) {
        i++;
    }

    /* Precision: 2 digits at most */
    if (fmt_str[i] == '.') {
        i++;
        if (isdigit(cast(uchar)fmt_str[i])) {
            i++;
        }
        if (isdigit(cast(uchar)fmt_str[i])) {
            i++;
        }
    }

    /* Still have width/precision remaining? */
    if (isdigit(cast(uchar)fmt_str[i])) {
        lulu_errorf(vm, "width/precision too long");
    }

    /* `i` points to '`x`' in `"04x"` so add 1 to copy it as well. */
    memcpy(&fmt_buf[1], fmt_str, i + 1);
    fmt_buf[i + 2] = '\0';
    return &fmt_str[i];
}

#define FMT_LEN_STR     "l"
#define FMT_LEN_SIZE    (sizeof(FMT_LEN_STR) - 1)
#define FMT_LEN_TYPE    long


/**
 * @brief
 *      Converts integer formats like `%i` into `%li`.
 */
static void
add_int_len(char (&fmt_buf)[FMT_BUFSIZE])
{
    size_t n = strlen(fmt_buf);
    char spec = fmt_buf[n - 1];
    /* `n - 1` points to `x` in `"%04x"`. */
    memcpy(&fmt_buf[n - 1], FMT_LEN_STR, FMT_LEN_SIZE);
    fmt_buf[(n - 1) + FMT_LEN_SIZE] = spec;
    fmt_buf[n + FMT_LEN_SIZE]       = '\0';
}

static void
add_quoted(lulu_VM *vm, lulu_Buffer *b, int argn)
{
    size_t i = 0, n = 0;
    const char *s = lulu_check_lstring(vm, argn, &n);
    lulu_write_char(b, '\"');
    for (i = 0; i < n; i++) {
        char ch = s[i];
        switch (ch) {
        case '\0': ch = '0'; goto write_escaped;
        case '\a': ch = 'a'; goto write_escaped;
        case '\b': ch = 'b'; goto write_escaped;
        case '\f': ch = 'f'; goto write_escaped;
        case '\t': ch = 't'; goto write_escaped;
        case '\n': ch = 'n'; goto write_escaped;
        case '\v': ch = 'v'; goto write_escaped;
        case '\r': ch = 'r'; goto write_escaped;
        case '\"':
        case '\\':
            write_escaped:
            lulu_write_char(b, '\\');
            [[fallthrough]];
        default:
            lulu_write_char(b, ch);
            break;
        }
    }
    lulu_write_char(b, '\"');
}

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
        char fmt_item[512];
        char fmt_buf[FMT_BUFSIZE];
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

        it    = get_format(vm, it, fmt_buf);
        start = it + 1;
        switch (*it) {
        case '%':
            lulu_write_char(&b, '%');
            /* skip flushing the buffer */
            continue;
        case 'c': {
            int ch = cast(int)lulu_check_integer(vm, argn);
            if (CHAR_MIN <= ch && ch <= CHAR_MAX) {
                written = sprintf(fmt_item, fmt_buf, ch);
            } else {
                sprintf(fmt_item, "unknown character code '%i'", ch);
                return lulu_arg_error(vm, argn, fmt_item);
            }
            break;
        }
        case 'd':
        case 'i': {
            lulu_Integer i = lulu_check_integer(vm, argn);
            add_int_len(fmt_buf);
            written = sprintf(fmt_item, fmt_buf, cast(FMT_LEN_TYPE)i);
            break;
        }
        case 'o':
        case 'u':
        case 'x': case 'X': {
            lulu_Integer i = lulu_check_integer(vm, argn);
            add_int_len(fmt_buf);
            written = sprintf(fmt_item, fmt_buf, cast(unsigned FMT_LEN_TYPE)i);
            break;
        }
        case 'e': case 'E':
        case 'f': case 'F':
        case 'g': case 'G': {
            lulu_Number n = lulu_check_number(vm, argn);
            /* Cast in case `lulu_Number` was configured to be non-`double` */
            written = sprintf(fmt_item, fmt_buf, cast(double)n);
            break;
        }
        case 'q':
            /* skip flushing the buffer */
            add_quoted(vm, &b, argn);
            continue;
        case 's': {
            size_t      l = 0;
            const char *s = lulu_check_lstring(vm, argn, &l);
            /* No precision given and string is too long to format? */
            if (strchr(fmt_buf, '.') == NULL && l >= 100) {
                lulu_write_lstring(&b, s, l);
                /* skip flushing the buffer */
                continue;
            }
            /**
             * @note(2025-07-27)
             *  Assumptions:
             *
             *      1.) We verified that the width is at most 2 digits,
             *          thus it is < 100.
             *      2.) Our worst-case (for the `fmt_item` buffer) is:
             *          `"%+99.99s"` or `"%-99.99s"`
             */
            written = sprintf(fmt_item, fmt_buf, s);
            break;
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
