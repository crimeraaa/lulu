#include <time.h>   // clock
#include <stdlib.h> // malloc, free
#include <stdio.h>  // fprintf
#include <string.h> // strcspn

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
        lulu_pop(vm, 1);
    }
    lulu_set_top(vm, 0);
}

static void
run_interactive(lulu_VM *vm)
{
    char line[512];
    for (;;) {
        printf(">>> ");
        if (fgets(line, (int)sizeof(line), stdin) == nullptr) {
            printf("\n");
            break;
        }

        size_t n = strcspn(line, "\r\n");
        run(vm, "stdin", line, n);
    }
}

static char *
read_file(const char *file_name, size_t *n)
{
    FILE    *file_ptr  = fopen(file_name, "rb+");
    char    *data = nullptr;
    size_t   n_read    = 0;
    long     file_size = 0;

    if (file_ptr == nullptr) {
        fprintf(stderr, "Failed to open file '%s'.\n", file_name);
        // Don't `fclose(NULL)`.
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
    if (data == nullptr) {
        fprintf(stderr, "Failed to allocate memory for file '%s'.\n", file_name);
        goto cleanup_buffer;
    }

    // `buffer[file_size]` is reserved for nul char so don't include it.
    n_read = fread(data, sizeof(data[0]), *n, file_ptr);
    if (n_read < *n) {
        fprintf(stderr, "Failed to read file '%s'.\n", file_name);
cleanup_buffer:
        free(data);
        data = nullptr;
        *n   = 0;
        goto cleanup_file;
    }

    data[n_read] = '\0'; // `n_read` can never be >= `buffer.len + 1`
cleanup_file:
    fclose(file_ptr);
cleanup_done:
    return data;
}

static void run_file(lulu_VM *vm, const char *file_name)
{
    size_t script_size;
    char *script = read_file(file_name, &script_size);
    if (script == nullptr) {
        return;
    }
    run(vm, file_name, script, script_size);
    free(script);
}

static int
base_print(lulu_VM *vm, int argc)
{
    for (int i = 1; i <= argc; i++) {
        if (i > 1) {
            fputc('\t', stdout);
        }
        switch (lulu_type(vm, i)) {
        case LULU_TYPE_NIL:
            fputs("nil", stdout);
            break;
        case LULU_TYPE_BOOLEAN:
            fputs(lulu_to_boolean(vm, i) ? "true" : "false", stdout);
            break;
        case LULU_TYPE_NUMBER:
            fprintf(stdout, LULU_NUMBER_FMT, lulu_to_number(vm, i));
            break;
        case LULU_TYPE_STRING:
            fputs(lulu_to_cstring(vm, i), stdout);
            break;
        case LULU_TYPE_TABLE:
        case LULU_TYPE_FUNCTION:
            fprintf(stdout, "%s: %p", lulu_type_name(vm, i), lulu_to_pointer(vm, i));
            break;
        default:
            __builtin_unreachable();
            break;
        }
    }
    fputc('\n', stdout);
    return 0;
}

static int
base_clock(lulu_VM *vm, int)
{
    lulu_Number n = lulu_Number(clock()) / lulu_Number(CLOCKS_PER_SEC);
    lulu_push_number(vm, n);
    return 1;
}

static void *
c_allocator(void *user_data, void *ptr, size_t old_size, size_t new_size)
{
    (void)user_data;
    (void)old_size;
    if (new_size == 0) {
        free(ptr);
        return nullptr;
    }
    return realloc(ptr, new_size);
}

int main(int argc, char *argv[])
{
    lulu_VM *vm = lulu_open(c_allocator, nullptr);
    lulu_push_cfunction(vm, base_print);
    lulu_set_global(vm, "print");

    lulu_push_cfunction(vm, base_clock);
    lulu_set_global(vm, "clock");

    int status = 0;
    switch (argc) {
    case 1:
        run_interactive(vm);
        break;
    case 2:
        run_file(vm, argv[1]);
        break;
    default:
        fprintf(stderr, "Usage: %s [script]\n", argv[0]);
        status = 1;
        break;
    }
    lulu_close(vm);
    return status;
}
