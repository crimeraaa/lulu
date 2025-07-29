#include <stdio.h>  /* fprintf */
#include <stdlib.h> /* malloc, free */
#include <string.h> /* strcspn */

#include "lulu.h"
#include "lulu_auxlib.h"


/**
 * @note(2025-07-29)
 *  Assumptions:
 *  
 *      1.) The current stack top is index 1.
 *      2.) The current stack top contains the script, as a `string`.
 */
static void
run(lulu_VM *vm, const char *source)
{
    size_t n = 0;
    const char *script = lulu_to_lstring(vm, 1, &n);
    lulu_Error e = lulu_load(vm, source, script, n);
    
    /* Remove `script` from stack; no longer needed. */
    lulu_remove(vm, 1);
    if (e == LULU_OK) {
        /* main function was pushed */
        e = lulu_pcall(vm, 0, LULU_MULTRET);
    }

    if (e != LULU_OK) {
        const char *msg = lulu_to_string(vm, -1);
        fprintf(stderr, "%s\n", msg);
    } else {
        /* successful call, so main function was overwritten with returns */
        int n = lulu_get_top(vm);
        if (n > 0) {
            lulu_get_global(vm, "print");
            lulu_insert(vm, 1);
            lulu_call(vm, n, 0);
        }
    }
    /* Remove main function and/or error message/s from stack */
    lulu_set_top(vm, 0);
}

static void
run_interactive(lulu_VM *vm)
{
    char line[512];
    for (;;) {
        size_t n = 0;
        printf(">>> ");
        if (fgets(line, (int)sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }
        n = strcspn(line, "\r\n");
        if (n > 0 && line[0] == '=') {
            lulu_push_fstring(vm, "return %s", line + 1);
        } else {
            lulu_push_lstring(vm, line, n);
        }
        run(vm, "stdin");
    }
}

static char *
read_file(const char *file_name, size_t *n)
{
    FILE    *file_ptr  = fopen(file_name, "rb+");
    char    *data      = NULL;
    size_t   n_read    = 0;
    long     file_size = 0;

    if (file_ptr == NULL) {
        fprintf(stderr, "Failed to open file '%s'.\n", file_name);
        /* Don't call `fclose(NULL)`. */
        return NULL;
    }

    fseek(file_ptr, 0L, SEEK_END);
    file_size = ftell(file_ptr);
    if (file_size == -1L) {
        fprintf(stderr, "Failed to determine size of file '%s'.\n", file_name);
        goto cleanup_file;
    }
    *n   = (size_t)(file_size);
    data = (char *)malloc(*n + 1);
    rewind(file_ptr);
    if (data == NULL) {
        fprintf(stderr, "Failed to allocate memory for file '%s'.\n", file_name);
        goto cleanup_buffer;
    }

    /* `data[file_size]` is reserved for nul char so don't include it. */
    n_read = fread(data, sizeof(data[0]), *n, file_ptr);
    if (n_read < *n) {
        fprintf(stderr, "Failed to read file '%s'.\n", file_name);
cleanup_buffer:
        free(data);
        data = NULL;
        *n   = 0;
        goto cleanup_file;
    }

    data[n_read] = '\0'; /* `n_read` can never be >= `buffer.len + 1` */
cleanup_file:
    fclose(file_ptr);
    return data;
}

static int
run_file(lulu_VM *vm, const char *file_name)
{
    size_t script_size;
    char  *script = read_file(file_name, &script_size);
    if (script == NULL) {
        return EXIT_FAILURE;
    }
    lulu_push_lstring(vm, script, script_size);
    run(vm, file_name);
    free(script);
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
    Main_Data *d = (Main_Data *)lulu_to_pointer(vm, 1);
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
    (void)user_data;
    (void)old_size;
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
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

    e = lulu_c_pcall(vm, protected_main, &d);
    lulu_close(vm);
    if (e == LULU_OK && d.status == EXIT_SUCCESS) {
        return EXIT_SUCCESS;
    } else if (e == LULU_ERROR_MEMORY) {
        return 2;
    }
    return EXIT_FAILURE;
}
