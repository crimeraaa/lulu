#include <ctype.h>  /* isspace */
#include <stdlib.h> /* strtoul */
#include <string.h>

#include "lulu_auxlib.h"

static int
base_assert(lulu_VM *L)
{
    int argc = lulu_get_top(L);
    lulu_check_any(L, 1);
    if (lulu_to_boolean(L, 1) == 0) {
        /* Minor optimization: retrieve message only when throwing. */
        const char *msg = lulu_opt_string(L, 2, "assertion failed!");
        return lulu_errorf(L, "%s", msg);
    }
    /* Return all arguments (even the error message, if any!) */
    return argc;
}

static int
base_type(lulu_VM *L)
{
    lulu_check_any(L, 1);
    lulu_push_string(L, lulu_type_name_at(L, 1));
    return 1;
}

static int
base_tostring(lulu_VM *L)
{
    lulu_Type t;
    lulu_check_any(L, 1);

    t = lulu_type(L, 1);
    switch (t) {
    case LULU_TYPE_NIL:
        lulu_push_literal(L, "nil");
        break;
    case LULU_TYPE_BOOLEAN:
        lulu_push_string(L, lulu_to_boolean(L, 1) ? "true" : "false");
        break;
    case LULU_TYPE_NUMBER: {
        size_t      n = 0;
        const char *s = lulu_to_lstring(L, 1, &n);
        lulu_push_lstring(L, s, n);
        break;
    }
    case LULU_TYPE_STRING:
        /* Already a string, so nothing to do. */
        break;
    default:
        lulu_push_fstring(L, "%s: %p", lulu_type_name_at(L, 1),
            lulu_to_pointer(L, 1));
        break;
    }
    return 1;
}

static int
base_tonumber(lulu_VM *L)
{
    int base = lulu_opt_integer(L, 2, /*def=*/10);

    /* Simple conversion to base-10? */
    if (base == 10) {
        /* Sanity check. Concept check: `tonumber();` (no arguments). */
        lulu_check_any(L, 1);

        /* number already or string convertible to number? */
        if (lulu_is_number(L, 1)) {
            lulu_Number n = lulu_to_number(L, 1);
            lulu_push_number(L, n);
            return 1;
        }
    }
    /* Conversion to non-base-10 integer is more involved. */
    else
    {
        /**
         * Even if input is already a number, convert it to a string so
         * we can re-parse it in the new base.
         */
        const char   *s = lulu_check_string(L, 1);
        char         *end;
        unsigned long ul;
        lulu_arg_check(L, 2 <= base && base <= 36, 2, "base out of range");
        ul = strtoul(s, &end, base);

        /**
         * According to the C standard, `*endptr == nptr` iff there were no
         * digits at all. So the opposite indicates we *might* have a valid
         * number string.
         */
        if (end != s) {
            /* Spaces are the only allowable trailing characters. */
            while (isspace(*end)) {
                end++;
            }

            /**
             * All valid trailing chars (if any) were skipped?
             * Concept check: return tonumber("1234  ", 16);
             */
            if (*end == '\0') {
                lulu_push_number(L, static_cast<lulu_Number>(ul));
                return 1;
            }
        }
    }
    /* Argument could not be converted to a number in the given base. */
    lulu_push_nil(L);
    return 1;
}

static int
base_print(lulu_VM *L)
{
    int i;
    int argc = lulu_get_top(L);
    lulu_get_global(L, "tostring"); /* ..., tostring */
    for (i = 1; i <= argc; i++) {
        if (i > 1) {
            fputc('\t', stdout);
        }
        lulu_push_value(L, -1); /* ..., tostring, tostring, */
        lulu_push_value(L, i);  /* ..., tostring, tostring, arg[i] */
        lulu_call(L, 1, 1);     /* ..., tostring, tostring(arg[i]) */
        fputs(lulu_to_string(L, -1), stdout);
        lulu_pop(L, 1); /* ..., tostring */
    }
    fputc('\n', stdout);
    return 0;
}

static int
base_next(lulu_VM *L)
{
    lulu_check_type(L, 1, LULU_TYPE_TABLE);
    lulu_set_top(L, 2); /* Create second argument (nil) if non provided. */
    int more = lulu_next(L, 1);
    if (more) {
        return 2;
    }
    lulu_push_nil(L);
    return 1;
}


static int
base_pairs(lulu_VM *L)
{
    lulu_check_type(L, 1, LULU_TYPE_TABLE);
    lulu_push_value(L, lulu_upvalue_index(1)); /* push generator */
    lulu_push_value(L, 1);                     /* push state */
    lulu_push_nil(L);                          /* push control initial value */
    return 3;
}

static int
ipairs_next(lulu_VM *L)
{
    lulu_Integer i;
    lulu_check_type(L, 1, LULU_TYPE_TABLE);
    i = lulu_check_integer(L, 2);
    i++;                      /* Next value. */
    lulu_push_integer(L, i); /* t, i, i+1 */
    lulu_push_value(L, -1);  /* t, i, i+1, i+1 */
    lulu_get_table(L, 1);    /* t, i, i+1, t[i+1] */
    if (lulu_is_nil(L, -1)) {
        return 0;
    }
    return 2;
}

static int
base_ipairs(lulu_VM *L)
{
    lulu_check_type(L, 1, LULU_TYPE_TABLE);
    lulu_push_value(L, lulu_upvalue_index(1)); /* push generator */
    lulu_push_value(L, 1);                     /* push state */
    lulu_push_integer(L, 0);                   /* push control initial value */
    return 3;
}

static void
push_iterator(lulu_VM *L, const char *name, lulu_CFunction f)
{
    lulu_push_cclosure(L, f, 1); /* _G, f ; setupvalue(f, 1, up) */
    lulu_set_field(L, -2, name); /* _G ; _G[name] = f */
}

static int
range_iterator(lulu_VM *L)
{
    lulu_Number state   = lulu_check_number(L, 1);
    lulu_Number control = lulu_check_number(L, 2);
    lulu_Number step    = lulu_check_number(L, lulu_upvalue_index(1));

    control += step;
    if ((step > 0) ? (control >= state) : (control <= state)) {
        return 0;
    }
    lulu_push_number(L, control);
    return 1;
}

static int
base_range(lulu_VM *L)
{
    lulu_Number start = lulu_check_number(L, 1);
    lulu_Number stop;
    /* for i in range(n) */
    if (lulu_is_none_or_nil(L, 2)) {
        stop  = start;
        start = 0;
    }
    /* for i in range(n, m) */
    else {
        stop = lulu_check_number(L, 2);
    }

    lulu_Number step = lulu_opt_number(L, 3, /*def=*/1);
    lulu_arg_check(L, step != 0, 3, "range step must be non-zero");

    /* 'step' is the sole upvalue of the iterator. It is popped when the
        closure is created. */
    lulu_push_number(L, step);

    lulu_push_cclosure(L, range_iterator, 1); /* push generator */
    lulu_push_number(L, stop);                /* push state */
    lulu_push_number(L, start - step);        /* push control initial value */
    return 3;
}

static const lulu_Register baselib[] = {
    {"print", base_print},
    {"assert", base_assert},
    {"tostring", base_tostring},
    {"tonumber", base_tonumber},
    {"type", base_type},
    {"next", base_next},
    {"range", base_range},
};

LULU_LIB_API int
lulu_open_base(lulu_VM *L)
{
    lulu_push_value(L, LULU_GLOBALS_INDEX); /* _G */
    lulu_set_global(L, "_G");               /* ; _G["_G"] = _G */
    lulu_set_library(L, "_G", baselib);     /* _G */

    /* Save memory by reusing global 'next' */
    lulu_get_field(L, -1, "next");
    push_iterator(L, "pairs", base_pairs);

    lulu_push_cfunction(L, ipairs_next);
    push_iterator(L, "ipairs", base_ipairs);
    return 1;
}
