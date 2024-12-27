/// local
#include "lulu.h"
#include "vm.h"

/// standard
#include <stdio.h>      // FILE, printf()
#include <stdlib.h>     // {m,re}alloc(), free()
#include <string.h>     // memset()

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

    // Trying to free some existing memory?
    // NOTE: old_size can be 0 in this case.
    if (new_size == 0) {
        free(old_ptr);
        return NULL;
    }

    // Otherwise, we're trying to [re]allocate some memory.
    void *new_ptr = realloc(old_ptr, new_size);

    // We extended the allocation? Note that immediately loading a possibly
    // invalid pointer is not a safe assumption for 100% of architectures.
    isize add_len = new_size - old_size;
    if (add_len > 0) {
        byte *add_ptr = cast(byte *)new_ptr + old_size;
        memset(add_ptr, 0, add_len);
    }
    return new_ptr;
}

static void
repl(lulu_VM *vm)
{
    char line[512];
    for (;;) {
        printf(">>> ");
        if (!fgets(line, size_of(line), stdin)) {
            printf("\n");
            break;
        }
        vm_interpret(vm, "stdin", line);
    }
}

static char *
read_file(lulu_VM *vm, cstring path, usize *out_size)
{
    FILE *file_ptr   = fopen(path, "rb");
    usize file_size  = 0;
    usize bytes_read = 0;
    usize buf_size   = 0;
    char *buf_ptr    = NULL;

    if (!file_ptr) {
        fprintf(stderr, "Failed to open file '%s'.\n", path);
        return NULL;
    }

    fseek(file_ptr, 0L, SEEK_END);
    file_size = ftell(file_ptr);
    rewind(file_ptr);

    buf_size = file_size + 1;
    if (out_size) {
        *out_size = buf_size;
    }

    buf_ptr = array_new(char, vm, buf_size);
    if (!buf_ptr) {
        fprintf(stderr, "Not enough memory to read file '%s'.\n", path);
        goto cleanup;
    }

    bytes_read = fread(buf_ptr, sizeof(buf_ptr[0]), file_size, file_ptr);
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
    usize       len    = 0;
    char       *source = read_file(vm, path, &len);
    lulu_Status status;

    if (!source) {
        return 2;
    }
    status = vm_interpret(vm, path, source);
    array_free(char, vm, source, len);

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
main(int argc, cstring argv[])
{
    lulu_VM vm;
    int     err = 0;
    if (!vm_init(&vm, alloc_fn, NULL)) {
        return 1;
    }
    if (argc == 1) {
        repl(&vm);
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
