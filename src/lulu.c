#include "lulu.h"
#include "chunk.h"
#include "debug.h"
#include "value.h"
#include "vm.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief
 *      A simple allocator that wraps the C standard `malloc` family.
 *      
 * @warning 2024-09-04
 *      This will call `abort()` on allocation failure!
 */
static void *heap_allocator_proc(void *allocator_data, isize new_size, isize align, void *old_ptr, isize old_size)
{
    void *new_ptr = NULL;
    isize add_len = new_size - old_size;

    unused(allocator_data);
    unused(align);

    // Trying to free some existing memory?
    if (new_size == 0 && old_size != 0) {
        free(old_ptr);
        return NULL;
    }

    // Otherwise, we're trying to [re]allocate some memory.
    new_ptr = realloc(old_ptr, new_size);

    // We extended the allocation? Note that immediately loading a possibly
    // invalid pointer is not a safe assumption for 100% of architectures.
    if (add_len > 0) {
        byte *add_ptr = cast(byte *)new_ptr + old_size;
        memset(add_ptr, 0, add_len);
    }
    return new_ptr;
}

static lulu_VM global_vm;
static lulu_VM *vm = &global_vm;

static void repl(void)
{
    char line[512];
    for (;;) {
        printf(">>> ");
        
        if (!fgets(line, size_of(line), stdin)) {
            printf("\n");
            break;
        }
        
        lulu_VM_interpret(vm, line);
    }
}

static char *read_file(cstring path, usize *out_size)
{
    FILE *file_ptr    = fopen(path, "rb");
    usize file_size   = 0;
    usize bytes_read  = 0;
    usize buffer_size = 0;
    char *buffer_ptr  = NULL;
    
    if (!file_ptr) {
        fprintf(stderr, "Failed to open file '%s'.\n", path);
        return NULL;
    }
    
    fseek(file_ptr, 0L, SEEK_END);
    file_size = ftell(file_ptr);
    rewind(file_ptr);

    buffer_size = file_size + 1;
    if (out_size) {
        *out_size = buffer_size;
    }

    buffer_ptr = rawarray_new(char, vm, buffer_size);
    if (!buffer_ptr) {
        fprintf(stderr, "Not enough memory to read file '%s'.\n", path);
        goto cleanup;
    }

    bytes_read = fread(buffer_ptr, sizeof(buffer_ptr[0]), file_size, file_ptr);
    if (bytes_read < file_size) {
        fprintf(stderr, "Could not read file '%s'.\n", path);
        rawarray_free(char, vm, buffer_ptr, buffer_size);
        goto cleanup;
    }

    buffer_ptr[bytes_read] = '\0';

cleanup:
    fclose(file_ptr);
    return buffer_ptr;
}

static int run_file(cstring path)
{
    usize       len    = 0;
    char       *source = read_file(path, &len);
    lulu_Status status;

    if (!source) {
        return 2;
    }
    status = lulu_VM_interpret(vm, source);
    rawarray_free(char, vm, source, len);

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

int main(int argc, cstring argv[])
{
    int err = 0;
    lulu_VM_init(vm, heap_allocator_proc, NULL);
    if (argc == 1) {
        repl();
    } else if (argc == 2) {
        err = run_file(argv[1]);
    } else {
        fprintf(stderr, "Usage: %s [script]\n", argv[0]);
        fflush(stderr);
        return 1;
    }
    lulu_VM_free(vm);
    return err;
}
