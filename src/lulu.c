/// local
#include "lulu.h"
#include "vm.h"

/// standard
#include <stdio.h>  // FILE, printf
#include <stdlib.h> // {m,re}alloc, free
#include <string.h> // memset, strcspn

/**
 * @brief
 *      A simple allocator that wraps the C standard `malloc` family.
 *
 * @note 2024-09-07
 *      This function does not throw errors by itself. Rather, calling
 *      `vm_run_protected()` will wrap most other calls with an error
 *      handler which will in turn catch errors such as out of memory.
 */
static void *
alloc_fn(void *allocator_data, isize new_size, isize align, void *old_ptr, isize old_size)
{
    unused(allocator_data);
    unused(align);
    unused(old_size);

    // Trying to free some existing memory?
    // NOTE: old_size can be 0 in this case.
    if (new_size == 0) {
        free(old_ptr);
        return NULL;
    }

    // Test to see if error handling works.
    // static int counter = 0;
    // if (counter == 32) {
    //     return NULL;
    // } else {
    //     counter++;
    // }

    // Otherwise, we're trying to [re]allocate some memory.
    void *new_ptr = realloc(old_ptr, cast(usize)new_size);
    return new_ptr;
}

static lulu_Status
run(lulu_VM *vm, const char *input, isize len, cstring file)
{
    lulu_Status status = vm_interpret(vm, input, len, file);
    if (status != LULU_OK) {
        fprintf(stderr, "%s\n", lulu_to_string(vm, -1));
    }
    return status;
}

static void
run_interactive(lulu_VM *vm)
{
    char line[512];
    for (;;) {
        fputs(">>> ", stdout);
        if (!fgets(line, size_of(line), stdin)) {
            fputc('\n', stdout);
            break;
        }
        run(vm, line, cast(isize)strcspn(line, "\n"), "stdin");
    }
}

static char *
read_file(lulu_VM *vm, cstring path, isize *out_len)
{
    FILE *file_ptr   = fopen(path, "rb");
    isize file_size  = 0;
    isize bytes_read = 0;
    isize buf_size   = 0;
    char *buf_ptr    = NULL;

    if (!file_ptr) {
        fprintf(stderr, "Failed to open file '%s'.\n", path);
        return NULL;
    }

    fseek(file_ptr, 0L, SEEK_END);
    file_size = cast(isize)ftell(file_ptr);
    rewind(file_ptr);
    if (out_len) {
        *out_len = file_size;
    }

    buf_size = file_size + 1;
    buf_ptr = array_new(char, vm, buf_size);
    if (!buf_ptr) {
        fprintf(stderr, "Not enough memory to read file '%s'.\n", path);
        goto cleanup;
    }

    bytes_read = cast(isize)fread(buf_ptr, sizeof(buf_ptr[0]), cast(usize)file_size, file_ptr);
    if (bytes_read < file_size) {
        fprintf(stderr, "Could not read file '%s'.\n", path);
        array_free(char, vm, buf_ptr, buf_size);
        goto cleanup;
    }
    buf_ptr[bytes_read] = '\0';

cleanup:
    fclose(file_ptr);
    return buf_ptr;
}

static int
run_file(lulu_VM *vm, cstring path)
{
    isize  len    = 0;
    char  *source = read_file(vm, path, &len);
    if (!source) {
        return 2;
    }
    lulu_Status status = run(vm, source, cast(isize)len, path);
    array_free(char, vm, source, cast(isize)(len + 1));

    switch (status) {
    case LULU_OK:
        return 0;
    case LULU_ERROR_COMPTIME:
    case LULU_ERROR_RUNTIME:
        return 1;
    case LULU_ERROR_MEMORY:
        return 2;
    }
    return 2;
}

int
main(int argc, char *argv[])
{
    lulu_VM vm;
    int     err = 0;
    if (!vm_init(&vm, &alloc_fn, NULL)) {
        return 1;
    }
    if (argc == 1) {
        run_interactive(&vm);
    } else if (argc == 2) {
        err = run_file(&vm, argv[1]);
    } else {
        fprintf(stderr, "Usage: %s [script]\n", argv[0]);
        fflush(stderr);
        return 1;
    }
    vm_free(&vm);
    return err;
}
