#include "lulu_auxlib.h"

#define table_check_arg(L, i)  lulu_check_type(L, i, LULU_TYPE_TABLE)

static void
write_field(lulu_VM *L, lulu_Buffer *b, lulu_Integer i)
{
    lulu_push_integer(L, i); /* t, i */
    lulu_get_table(L, 1);    /* t, t[i] ; pop i */
    size_t n;
    const char *s = lulu_to_lstring(L, -1, &n);
    lulu_write_lstring(b, s, n);
    lulu_pop(L, 1); /* t ; pop t[i] */
}

static int
table_concat(lulu_VM *L)
{
    size_t sep_n;
    const char *sep;
    lulu_Integer i, j;

    table_check_arg(L, 1);
    sep = lulu_opt_lstring(L, 2, "", &sep_n);
    i = lulu_opt_integer(L, 3, /*def=*/1);
    j = lulu_opt_integer(L, 4, /*def=*/lulu_obj_len(L, 1));

    lulu_Buffer b;
    lulu_buffer_init(L, &b);
    for (; i < j; i++) {
        write_field(L, &b, i);
        lulu_write_lstring(&b, sep, sep_n);
    }
    /* Add last value? Only proceeds if interval j was not empty. */
    if (i == j) {
        write_field(L, &b, i);
    }
    lulu_finish_string(&b);
    return 1;
}

static const lulu_Register
table_library[] = {
    {"concat", table_concat}
};

LULU_LIB_API int
lulu_open_table(lulu_VM *L)
{
    lulu_set_library(L, LULU_TABLE_LIB_NAME, table_library);
    return 1;
}
