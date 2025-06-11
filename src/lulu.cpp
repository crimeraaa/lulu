#include <stdlib.h>
#include <stdio.h>

#include "lulu.h"
#include "chunk.hpp"
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
        vm_interpret(vm, string_make("stdin"), string_make(line, n));
    }
}

static Slice<char>
read_file(const char *file_name)
{
    FILE       *file_ptr  = fopen(file_name, "rb+");
    Slice<char> buffer{nullptr, 0}; // `goto` cannot skip over variables.
    size_t      n_read = 0;

    if (file_ptr == nullptr) {
        fprintf(stderr, "Failed to open file '%s'.\n", file_name);
        goto cleanup_file;
    }

    fseek(file_ptr, 0L, SEEK_END);
    buffer.len = cast(size_t, ftell(file_ptr)) + 1;
    rewind(file_ptr);

    buffer.data = cast(char *, malloc(buffer.len));
    if (buffer.data == nullptr) {
        fprintf(stderr, "Failed to allocate memory for file '%s'.\n", file_name);
        goto cleanup_buffer;
    }

    n_read = fread(raw_data(buffer), sizeof(buffer.data[0]), buffer.len - 1, file_ptr);
    if (n_read < buffer.len) {
        fprintf(stderr, "Failed to read file '%s'.\n", file_name);
cleanup_buffer:
        free(raw_data(buffer));
        buffer.data = nullptr;
        buffer.len  = 0;
        goto cleanup_file;
    }

    // `n_read` can never be >= `buffer.len + 1`
    buffer[n_read] = '\0';
cleanup_file:
    fclose(file_ptr);
    return buffer;
}

static void run_file(lulu_VM &vm, const char *file_name)
{
    Slice<char> script = read_file(file_name);
    vm_interpret(vm, string_make(file_name), string_make(script));
    free(raw_data(script));
}


int main(int argc, char *argv[])
{
    lulu_VM vm;
    vm_init(vm, c_allocator, nullptr);
    switch (argc) {
    case 1:
        run_interactive(vm);
        break;
    case 2:
        run_file(vm, argv[1]);
        break;
    default:
        fprintf(stderr, "Usage: %s [script]\n", argv[0]);
        break;
    }
    vm_destroy(vm);
}
