#include <time.h>   /* clock */
#include <stdlib.h> /* malloc, free */
#include <stdio.h>  /* fprintf */
#include <string.h> /* strcspn */

#include "lulu.h"

static void
run(lulu_VM *vm, const char *source, const char *script, size_t n)
{
    lulu_Error e = lulu_load(vm, source, script, n);
    if (e == LULU_OK) {
        e = lulu_pcall(vm, 0, 0);
    }

    if (e != LULU_OK) {
        const char *msg = lulu_to_cstring(vm, -1);
        fprintf(stderr, "%s\n", msg);
        /* lulu_{load,pcall} leave an error message on the top of the stack */
        lulu_pop(vm, 1);
    }
    /* LULU_RUNTIME_ERROR leaves the main function on top of the stack */
    lulu_set_top(vm, 0);
}

static void
run_interactive(lulu_VM *vm)
{
    char line[512];
    for (;;) {
        printf(">>> ");
        if (fgets(line, (int)sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        run(vm, "stdin", line, strcspn(line, "\r\n"));
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
        /* Don't `fclose(NULL)`. */
        goto cleanup_done;
    }

    fseek(file_ptr, 0L, SEEK_END);
    file_size = ftell(file_ptr);
    if (file_size == -1L) {
        fprintf(stderr, "Failed to determine size of file '%s'.\n", file_name);
        goto cleanup_file;
    }
    *n  = (size_t)(file_size);
    data = (char *)malloc(*n + 1);
    rewind(file_ptr);
    if (data == NULL) {
        fprintf(stderr, "Failed to allocate memory for file '%s'.\n", file_name);
        goto cleanup_buffer;
    }

    /* `buffer[file_size]` is reserved for nul char so don't include it. */
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
cleanup_done:
    return data;
}

static int
run_file(lulu_VM *vm, const char *file_name)
{
    size_t script_size;
    char *script = read_file(file_name, &script_size);
    if (script == NULL) {
        return EXIT_FAILURE;
    }
    run(vm, file_name, script, script_size);
    free(script);
    return EXIT_SUCCESS;
}

static int
base_tostring(lulu_VM *vm, int argc)
{
    if (argc != 1) {
        lulu_push_fstring(vm, "Expected 1 argument to `tostring`, got %i", argc);
        lulu_error(vm);
        return 1;
    }
    switch (lulu_type(vm, 1)) {
    case LULU_TYPE_NIL:
        lulu_push_literal(vm, "nil");
        break;
    case LULU_TYPE_BOOLEAN:
        lulu_push_cstring(vm, lulu_to_boolean(vm, 1) ? "true" : "false");
        break;
    case LULU_TYPE_NUMBER:
        lulu_push_fstring(vm, "%f", lulu_to_number(vm, 1));
        break;
    case LULU_TYPE_STRING:
        /* Nothing to do */
        break;
    case LULU_TYPE_USERDATA:
    case LULU_TYPE_TABLE:
    case LULU_TYPE_FUNCTION: {
        const char *s;
        void *p;
        s = lulu_type_name(vm, 1);
        p = lulu_to_pointer(vm, 1);
        lulu_push_fstring(vm, "%s: %p", s, p);
        break;
    }
    default:
        __builtin_unreachable();
        break;
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
        fprintf(stdout, "%s", lulu_to_cstring(vm, -1));
        lulu_pop(vm, 1);         /* ..., tostring */
    }
    fputc('\n', stdout);
    return 0;
}

static int
base_clock(lulu_VM *vm, int argc)
{
    lulu_Number n;
    (void)argc;
    n = (lulu_Number)clock() / (lulu_Number)CLOCKS_PER_SEC;
    lulu_push_number(vm, n);
    return 1;
}

static const lulu_Register
baselib[] = {
    {"clock",    base_clock},
    {"tostring", base_tostring},
    {"print",    base_print},
};

typedef struct {
    char **argv;
    int    argc;
    int    status;
} Main_Data;

static int
protected_main(lulu_VM *vm, int argc)
{
    Main_Data *d;
    (void)argc;
    d = (Main_Data *)lulu_to_pointer(vm, 1);

    /* stack can be cleared at this point, `Main_Data` is a C type so it
    cannot be collected no matter what. */
    lulu_register(vm, baselib, sizeof(baselib) / sizeof(baselib[0]));
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
    Main_Data  d = {argv, argc, EXIT_SUCCESS};
    lulu_VM   *vm;
    lulu_Error e;

    vm = lulu_open(c_allocator, NULL);
    e  = lulu_c_pcall(vm, protected_main, &d);
    lulu_close(vm);

    if (e == LULU_OK && d.status == EXIT_SUCCESS) {
        return EXIT_SUCCESS;
    } else if (e == LULU_ERROR_MEMORY) {
        return 2;
    }
    return EXIT_FAILURE;
}
