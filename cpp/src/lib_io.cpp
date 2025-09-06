#include <string.h>
#include <errno.h>

#include "lulu_auxlib.h"

#define cast(T)     (T)

#define MT_NAME  "lulu.io.FILE *"

static void
io_file_new(lulu_VM *L, FILE *file)
{
    /* Layout: | <userdata> | FILE * | */
    FILE **ud = cast(FILE **)lulu_new_userdata(L, sizeof(FILE *));
    *ud = file;
    lulu_get_library_metatable(L, MT_NAME);
    lulu_set_metatable(L, -2);
}

static int
io_open(lulu_VM *L)
{
    const char *name = lulu_check_string(L, 1);
    const char *mode = lulu_opt_string(L, 2, /*def=*/"r");
    FILE *file = fopen(name, mode);
    if (file == nullptr) {
        int e = errno;
        lulu_push_nil(L);
        lulu_push_fstring(L, "%s: %s", name, strerror(e));
    } else {
        io_file_new(L, file);
    }
    return 1;
}

static FILE **
io_check_arg(lulu_VM *L, int i)
{
    FILE **ud = cast(FILE **)lulu_check_userdata(L, i, MT_NAME);
    if (*ud == nullptr) {
        lulu_errorf(L, "Attempt to use a closed file");
    }
    return ud;
}

static int
io_close(lulu_VM *L)
{
    FILE **ud = io_check_arg(L, 1);
    fclose(*ud);
    *ud = nullptr;
    return 0;
}

static int
io_write(lulu_VM *L)
{
    FILE *f = *io_check_arg(L, 1);
    int n_args = lulu_get_top(L);
    for (int i = 2; i <= n_args; i++) {
        if (lulu_type(L, i) == LULU_TYPE_NUMBER) {
            fprintf(f, LULU_NUMBER_FMT, lulu_to_number(L, i));
        } else {
            size_t n;
            const char *s = lulu_check_lstring(L, i, &n);
            fwrite(s, sizeof(char), n, f);
        }
    }
    return 0;
}

static int
io_tostring(lulu_VM *L)
{
    FILE *ud = *cast(FILE **)lulu_check_userdata(L, 1, MT_NAME);
    if (ud == nullptr) {
        lulu_push_literal(L, "file closed");
    } else {
        lulu_push_fstring(L, "file (%p)", cast(void *)ud);
    }
    return 1;
}

static const lulu_Register
io_library[] = {
    {"open", io_open},
    {"close", io_close},
    {"write", io_write},
};

static const lulu_Register
io_methods[] = {
    {"close", io_close},
    {"write", io_write},
    {"__tostring", io_tostring},
};

LULU_LIB_API int
lulu_open_io(lulu_VM *L)
{
    lulu_new_metatable(L, MT_NAME);        /* mt */
    lulu_push_value(L, -1);                /* mt, mt */
    lulu_set_field(L, -2, "__index");      /* mt ; mt.__index = mt */
    lulu_set_library(L, NULL, io_methods); /* mt */

    lulu_set_library(L, LULU_IO_LIB_NAME, io_library);
    io_file_new(L, stdin);
    lulu_set_field(L, -2, "stdin");

    io_file_new(L, stdout);
    lulu_set_field(L, -2, "stdout");

    io_file_new(L, stderr);
    lulu_set_field(L, -2, "stderr");
    return 1;
}
