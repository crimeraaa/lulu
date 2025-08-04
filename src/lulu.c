#include <stdio.h>  /* fprintf */
#include <stdlib.h> /* malloc, free */
#include <string.h> /* strcspn */

#include "lulu.h"
#include "lulu_auxlib.h"

#define cast(T)     (T)

static void
report_error(lulu_VM *vm)
{
    if (!lulu_is_nil(vm, -1)) {
        const char *msg = lulu_to_string(vm, -1);
        if (msg == NULL) {
            msg = "(error object is not a string)";
        }
        fprintf(stderr, "%s\n", msg);
        lulu_pop(vm, 1); /* Remove error message from stack. */
    }
}

/**
 * @note(2025-07-29)
 *  Assumptions:
 *
 *      1.) The current stack top is index `1`.
 *      2.) It contains the 'main' `function`.
 */
static void
run(lulu_VM *vm)
{
    lulu_Error e = lulu_pcall(vm, 0, LULU_MULTRET);
    if (e == LULU_OK) {
        /* successful call, so main function was overwritten with returns */
        int n = lulu_get_top(vm);
        if (n > 0) {
            lulu_get_global(vm, "print");
            lulu_insert(vm, 1);
            lulu_call(vm, n, 0);
        }
    } else {
        report_error(vm);
    }
    /* If `e == LULU_ERROR_RUNTIME`, main function is still on top. */
    lulu_set_top(vm, 0);
}

typedef struct {
    const char *data;
    size_t      len;
} Reader_Line;

static const char *
reader_line(void *user_ptr, size_t *n)
{
    Reader_Line *r = cast(Reader_Line *)user_ptr;
    const char *s = r->data;
    *n = r->len;

    /* First time reading this line? */
    if (s != NULL) {
        /* Mark as read; subsequent calls will return sentinel data. */
        r->data = NULL;
        r->len  = 0;
    }
    return s;
}

static void
run_interactive(lulu_VM *vm)
{
    for (;;) {
        char        buf[512];
        Reader_Line r;
        lulu_Error  e;
        size_t      n = 0;

        printf(">>> ");
        if (fgets(buf, (int)sizeof(buf), stdin) == NULL) {
            printf("\n");
            break;
        }

        n = strcspn(buf, "\r\n");
        buf[n] = '\0'; /* prevent `lulu_push_fstring()` from including '\n' */
        if (n > 0 && buf[0] == '=') {
            lulu_push_fstring(vm, "return %s", buf + 1);
        } else {
            lulu_push_lstring(vm, buf, n);
        }

        r.data = lulu_to_lstring(vm, 1, &r.len);
        e = lulu_load(vm, "stdin", reader_line, &r);
        lulu_remove(vm, 1); /* Remove line. */
        if (e == LULU_OK) {
            run(vm);
        } else {
            report_error(vm);
        }
    }
}

typedef struct {
    FILE  *file;
    char   buffer[LULU_BUFFER_BUFSIZE];
} Reader_File;

static const char *
reader_file(void *user_ptr, size_t *n)
{
    Reader_File *r = cast(Reader_File *)user_ptr;

    /* No more to read? */
    if (feof(r->file)) {
        *n = 0;
        return NULL;
    }
    /* Fill buffer. Note for small files this may be the only time! */
    *n = fread(r->buffer, 1, sizeof(r->buffer), r->file);
    /* Read successfully? */
    return (*n > 0) ? r->buffer : NULL;
}

static int
run_file(lulu_VM *vm, const char *file_name)
{
    Reader_File r;
    lulu_Error  e;
    r.file = fopen(file_name, "r");
    if (r.file == NULL) {
        return EXIT_FAILURE;
    }
    e = lulu_load(vm, file_name, reader_file, &r);
    fclose(r.file);
    if (e == LULU_OK) {
        run(vm);
    } else {
        report_error(vm);
    }
    return EXIT_SUCCESS;
}

typedef struct {
    char **argv;
    int    argc;
    int    status;
} Main_Data;

static int
protected_main(lulu_VM *vm)
{
    Main_Data *d = cast(Main_Data *)lulu_to_pointer(vm, 1);
    lulu_open_libs(vm);
    /* Don't include userdata when printing REPL results. */
    lulu_set_top(vm, 0);
    switch (d->argc) {
    case 1:
        run_interactive(vm);
        break;
    case 2:
        d->status = run_file(vm, d->argv[1]);
        break;
    default:
        fprintf(stderr, "Usage: %s [script]\n", d->argv[0]);
        d->status = EXIT_FAILURE;
        break;
    }
    return 0;
}

static void *
c_allocator(void *user_data, void *ptr, size_t old_size, size_t new_size)
{
    cast(void)user_data;
    cast(void)old_size;
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

static int
panic(lulu_VM *vm)
{
    const char *msg = lulu_to_string(vm, -1);
    fprintf(stderr, "[FATAL]: Unprotected call to Lulu API (%s)\n", msg);
    return 0;
}

int main(int argc, char *argv[])
{
    Main_Data  d;
    lulu_VM   *vm;
    lulu_Error e;
    /* In C89, brace initialization requires all constant expressions. */
    d.argv   = argv;
    d.argc   = argc;
    d.status = EXIT_SUCCESS;

    vm = lulu_open(c_allocator, NULL);
    if (vm == NULL) {
        fprintf(stderr, "Failed to allocate memory for lulu\n");
        return 2;
    }
    lulu_set_panic(vm, panic);

    /* Testing to see if panic works. */
    /* lulu_check_string(vm, 1); */

    e = lulu_c_pcall(vm, protected_main, &d);
    lulu_close(vm);
    if (e == LULU_OK && d.status == EXIT_SUCCESS) {
        return EXIT_SUCCESS;
    } else if (e == LULU_ERROR_MEMORY) {
        return 2;
    }
    return EXIT_FAILURE;
}
