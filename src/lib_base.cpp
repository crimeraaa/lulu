#include <string.h>
#include <time.h>

#include "lulu_auxlib.h"

static int
base_clock(lulu_VM *vm, int argc)
{
    lulu_Number n;
    (void)argc;
    n = (lulu_Number)clock() / (lulu_Number)CLOCKS_PER_SEC;
    lulu_push_number(vm, n);
    return 1;
}

static int
base_type(lulu_VM *vm, int argc)
{
    lulu_Type   t;
    const char *s;
    size_t      n;

    (void)argc;
    lulu_check_any(vm, 1);

    t = lulu_type(vm, 1);
    s = lulu_type_name(vm, t);
    n = strlen(s);
    lulu_push_lstring(vm, s, n);
    return 1;
}

static int
base_tostring(lulu_VM *vm, int argc)
{
    lulu_Type t;
    (void)argc;
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
        size_t n;
        const char *s = lulu_to_lstring(vm, 1, &n);
        lulu_push_lstring(vm, s, n);
        break;
    }
    case LULU_TYPE_STRING:
        /* Already a string, so nothing to do. */
        break;
    default:
        lulu_push_fstring(vm, "%s: %p",
            lulu_type_name_at(vm, 1), lulu_to_pointer(vm, 1));
        break;
    }
    return 1;
}

static int
base_tonumber(lulu_VM *vm, int argc)
{
    lulu_Number n;
    (void)argc;
    lulu_check_any(vm, 1);

    /* Convert first, ask questions later */
    n = lulu_to_number(vm, 1);
    if (n == 0 && !lulu_is_number(vm, 1)) {
        lulu_push_nil(vm, 1);
    } else {
        lulu_push_number(vm, n);
    }
    return 1;
}

static int
base_print(lulu_VM *vm, int argc)
{
    int i;
    lulu_get_global(vm, "tostring"); /* ..., tostring */
    for (i = 1; i <= argc; i++) {
        if (i > 1) {
            fputc('\t', stdout);
        }
        lulu_push_value(vm, -1); /* ..., tostring, tostring, */
        lulu_push_value(vm, i);  /* ..., tostring, tostring, arg[i] */
        lulu_call(vm, 1, 1);     /* ..., tostring, tostring(arg[i]) */
        fprintf(stdout, "%s", lulu_to_string(vm, -1));
        lulu_pop(vm, 1);         /* ..., tostring */
    }
    fputc('\n', stdout);
    return 0;
}

static const lulu_Register
baselib[] = {
    {"clock",       base_clock},
    {"tostring",    base_tostring},
    {"tonumber",    base_tonumber},
    {"print",       base_print},
    {"type",        base_type},
};

LULU_API int
lulu_open_base(lulu_VM *vm, int argc)
{
    int i;
    const char *libname = lulu_to_string(vm, 1);
    (void)argc;

    /* expose _G to user */
    lulu_push_value(vm, LULU_GLOBALS_INDEX);
    lulu_set_global(vm, "_G");

    /* Stack: base */
    lulu_set_library(vm, libname, baselib, lulu_count_library(baselib));

    /* copy `base` into `_G`. */
    for (i = 0; i < lulu_count_library(baselib); i++) {
        const char *s = baselib[i].name;
        size_t      n = strlen(s);
        lulu_push_lstring(vm, s, n); /* base, key */
        lulu_get_table(vm, -2);      /* base, base[key] */
        lulu_set_global(vm, s);      /* base ; _G[key] = base[key] */
    }
    return 1;
}
