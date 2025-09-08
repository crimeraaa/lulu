#include <string.h>
#include <errno.h>

#include "lulu_auxlib.h"

#define cast(T)     (T)

#define MT_NAME  "lulu.io.FILE *"

static FILE **
io_file_new(lulu_VM *L)
{
    /* Layout: | <userdata> | FILE * | */
    FILE **ud = cast(FILE **)lulu_new_userdata(L, sizeof(FILE *));
    lulu_get_library_metatable(L, MT_NAME);
    lulu_set_metatable(L, -2);
    return ud;
}

static int
io_push_result(lulu_VM *L, int status, const char *file_name)
{
    int e = errno;
    if (status) {
        lulu_push_boolean(L, 1);
        return 1;
    }
    lulu_push_nil(L);
    if (file_name != nullptr) {
        lulu_push_fstring(L, "%s: %s", file_name, strerror(e));
    } else {
        lulu_push_fstring(L, "%s", strerror(e));
    }
    lulu_push_integer(L, e);
    return 3;
}

static int
io_open(lulu_VM *L)
{
    const char *name = lulu_check_string(L, 1);
    const char *mode = lulu_opt_string(L, 2, /*def=*/"r");
    FILE **ud = io_file_new(L);
    *ud = fopen(name, mode);
    if (*ud == nullptr) {
        return io_push_result(L, false, name);
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
    int status = 1;
    for (int i = 2; i <= n_args; i++) {
        if (lulu_type(L, i) == LULU_TYPE_NUMBER) {
            lulu_Number n = lulu_to_number(L, i);
            status = status && fprintf(f, LULU_NUMBER_FMT, n);
        } else {
            size_t n;
            const char *s = lulu_check_lstring(L, i, &n);
            status = status && (fwrite(s, sizeof(*s), n, f) == n);
        }
    }
    return io_push_result(L, status, nullptr);
}

static int
io_flush(lulu_VM *L)
{
    FILE *f = *io_check_arg(L, 1);
    int status = fflush(f);
    return io_push_result(L, status == 0, nullptr);
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
    {"write", io_write}, /** @todo(2025-09-06) Write to stdout directly */
    {"flush", io_flush},
};

static const lulu_Register
io_methods[] = {
    {"close", io_close},
    {"write", io_write},
    {"flush", io_flush},
    {"__tostring", io_tostring},
};

static void
io_open_std(lulu_VM *L, FILE *f, const char *name)
{
    FILE **ud = io_file_new(L);
    *ud = f;
    lulu_set_field(L, -2, name);
}

LULU_LIB_API int
lulu_open_io(lulu_VM *L)
{
    lulu_new_metatable(L, MT_NAME);        /* mt */
    lulu_push_value(L, -1);                /* mt, mt */
    lulu_set_field(L, -2, "__index");      /* mt ; mt.__index = mt */
    lulu_set_library(L, NULL, io_methods); /* mt */

    lulu_set_library(L, LULU_IO_LIB_NAME, io_library);
    io_open_std(L, stdin, "stdin");
    io_open_std(L, stdout, "stdout");
    io_open_std(L, stderr, "stderr");
    return 1;
}
