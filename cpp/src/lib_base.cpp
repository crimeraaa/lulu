#include <ctype.h>  /* isspace */
#include <stdlib.h> /* strtoul */
#include <string.h>

#include "lulu_auxlib.h"

static int
base_assert(lulu_VM *vm)
{
    int argc = lulu_get_top(vm);
    lulu_check_any(vm, 1);
    if (lulu_to_boolean(vm, 1) == 0) {
        /* Minor optimization: retrieve message only when throwing. */
        const char *msg = lulu_opt_string(vm, 2, "assertion failed!");
        return lulu_errorf(vm, "%s", msg);
    }
    /* Return all arguments (even the error message, if any!) */
    return argc;
}

static int
base_type(lulu_VM *vm)
{
    lulu_check_any(vm, 1);
    lulu_push_string(vm, lulu_type_name_at(vm, 1));
    return 1;
}

static int
base_tostring(lulu_VM *vm)
{
    lulu_Type t;
    lulu_check_any(vm, 1);

    t = lulu_type(vm, 1);
    switch (t) {
    case LULU_TYPE_NIL:
        lulu_push_literal(vm, "nil");
        break;
    case LULU_TYPE_BOOLEAN:
        lulu_push_string(vm, lulu_to_boolean(vm, 1) ? "true" : "false");
        break;
    case LULU_TYPE_NUMBER: {
        size_t      n = 0;
        const char *s = lulu_to_lstring(vm, 1, &n);
        lulu_push_lstring(vm, s, n);
        break;
    }
    case LULU_TYPE_STRING:
        /* Already a string, so nothing to do. */
        break;
    default:
        lulu_push_fstring(
            vm,
            "%s: %p",
            lulu_type_name_at(vm, 1),
            lulu_to_pointer(vm, 1)
        );
        break;
    }
    return 1;
}

static int
base_tonumber(lulu_VM *vm)
{
    int base = lulu_opt_integer(vm, 2, /* def */ 10);

    /* Simple conversion to base-10? */
    if (base == 10) {
        /* Sanity check. Concept check: `tonumber();` (no arguments). */
        lulu_check_any(vm, 1);

        /* number already or string convertible to number? */
        if (lulu_is_number(vm, 1)) {
            lulu_Number n = lulu_to_number(vm, 1);
            lulu_push_number(vm, n);
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
        const char   *s = lulu_check_string(vm, 1);
        char         *end;
        unsigned long ul;
        lulu_arg_check(vm, 2 <= base && base <= 36, 2, "base out of range");
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
                lulu_push_number(vm, static_cast<lulu_Number>(ul));
                return 1;
            }
        }
    }
    /* Argument could not be converted to a number in the given base. */
    lulu_push_nil(vm);
    return 1;
}

static int
base_print(lulu_VM *vm)
{
    int i;
    int argc = lulu_get_top(vm);
    lulu_get_global(vm, "tostring"); /* ..., tostring */
    for (i = 1; i <= argc; i++) {
        if (i > 1) {
            fputc('\t', stdout);
        }
        lulu_push_value(vm, -1); /* ..., tostring, tostring, */
        lulu_push_value(vm, i);  /* ..., tostring, tostring, arg[i] */
        lulu_call(vm, 1, 1);     /* ..., tostring, tostring(arg[i]) */
        fputs(lulu_to_string(vm, -1), stdout);
        lulu_pop(vm, 1); /* ..., tostring */
    }
    fputc('\n', stdout);
    return 0;
}

static int
base_next(lulu_VM *vm)
{
    lulu_check_type(vm, 1, LULU_TYPE_TABLE);
    lulu_set_top(vm, 2); /* Create second argument (nil) if non provided. */
    int more = lulu_next(vm, 1);
    if (more) {
        return 2;
    }
    lulu_push_nil(vm);
    return 1;
}


static int
base_pairs(lulu_VM *vm)
{
    lulu_check_type(vm, 1, LULU_TYPE_TABLE);
    lulu_push_value(vm, lulu_upvalue_index(1)); /* push generator */
    lulu_push_value(vm, 1);                     /* push state */
    lulu_push_nil(vm);                          /* push control initial value */
    return 3;
}

static int
ipairs_next(lulu_VM *vm)
{
    lulu_Integer i;
    lulu_check_type(vm, 1, LULU_TYPE_TABLE);
    i = lulu_check_integer(vm, 2);
    i++;                      /* Next value. */
    lulu_push_integer(vm, i); /* t, i, i+1 */
    lulu_push_value(vm, -1);  /* t, i, i+1, i+1 */
    lulu_get_table(vm, 1);    /* t, i, i+1, t[i+1] */
    if (lulu_is_nil(vm, -1)) {
        return 0;
    }
    return 2;
}

static int
base_ipairs(lulu_VM *vm)
{
    lulu_check_type(vm, 1, LULU_TYPE_TABLE);
    lulu_push_value(vm, lulu_upvalue_index(1)); /* push generator */
    lulu_push_value(vm, 1);                     /* push state */
    lulu_push_integer(vm, 0);                   /* push control initial value */
    return 3;
}

static void
push_iterator(lulu_VM *vm, const char *name, lulu_CFunction f)
{
    lulu_push_cclosure(vm, f, 1); /* _G, f ; setupvalue(f, 1, up) */
    lulu_set_field(vm, -2, name); /* _G ; _G[name] = f */
}

static const lulu_Register baselib[] = {
    {"assert", base_assert},
    {"tostring", base_tostring},
    {"tonumber", base_tonumber},
    {"print", base_print},
    {"type", base_type},
    {"next", base_next},
};

LULU_LIB_API int
lulu_open_base(lulu_VM *vm)
{
    lulu_push_value(vm, LULU_GLOBALS_INDEX); /* _G */
    lulu_set_global(vm, "_G");               /* ; _G["_G"] = _G */
    lulu_set_library(vm, "_G", baselib);     /* _G */

    /* Save memory by reusing global 'next' */
    lulu_get_field(vm, -1, "next");
    push_iterator(vm, "pairs", base_pairs);

    lulu_push_cfunction(vm, ipairs_next);
    push_iterator(vm, "ipairs", base_ipairs);
    return 1;
}
