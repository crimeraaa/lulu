#include <stdlib.h>
#include <stdio.h>

#include "lulu.h"
#include "vm.hpp"
#include "debug.hpp"

static void *
c_allocator(void *user_data, void *ptr, size_t old_size, size_t new_size)
{
    unused(user_data);
    unused(old_size);
    if (new_size == 0) {
        free(ptr);
        return nullptr;
    }
    return realloc(ptr, new_size);
}

static void
run_interactive(lulu_VM &vm)
{
    char line[512];
    for (;;) {
        printf(">>> ");
        if (fgets(line, cast_int(count_of(line)), stdin) == nullptr) {
            printf("\n");
            break;
        }

        size_t n = strcspn(line, "\r\n");
        vm_interpret(vm, "stdin"_s, String(line, n));
    }
}

static Slice<char>
read_file(const char *file_name)
{
    FILE       *file_ptr  = fopen(file_name, "rb+");
    Slice<char> buffer; // `goto` cannot skip over variables.
    size_t      n_read    = 0;
    long        file_size = 0;

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
    buffer.len  = (size_t)(file_size) + 1;
    buffer.data = (char *)malloc(len(buffer));
    rewind(file_ptr);
    if (raw_data(buffer) == nullptr) {
        fprintf(stderr, "Failed to allocate memory for file '%s'.\n", file_name);
        goto cleanup_buffer;
    }

    // `buffer[file_size]` is reserved for nul char so don't include it.
    n_read = fread(raw_data(buffer), sizeof(buffer.data[0]), len(buffer) - 1, file_ptr);
    if (n_read < len(buffer) - 1) {
        fprintf(stderr, "Failed to read file '%s'.\n", file_name);
cleanup_buffer:
        free(raw_data(buffer));
        buffer.data = nullptr;
        buffer.len  = 0;
        goto cleanup_file;
    }

    buffer[n_read] = '\0'; // `n_read` can never be >= `buffer.len + 1`
    buffer.len--;          // Don't include the nul char in the final count.
cleanup_file:
    fclose(file_ptr);
cleanup_done:
    return buffer;
}

static void run_file(lulu_VM &vm, const char *file_name)
{
    Slice<char> script = read_file(file_name);
    if (raw_data(script) == nullptr) {
        return;
    }
    vm_interpret(vm, String(file_name), String(script));
    free(raw_data(script));
}


int main(int argc, char *argv[])
{
    lulu_VM vm;
    vm_init(vm, c_allocator, nullptr);
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
    vm_destroy(vm);
    return status;
}
