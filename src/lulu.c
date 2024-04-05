#include "lulu.h"
#include "chunk.h"
#include "debug.h"
#include "limits.h"
#include "vm.h"

#include <sysexits.h>

VM global_vm = {0};

static int repl(VM *vm) {
    char line[LULU_MAXLINE];
    for (;;) {
        fputs(LULU_PROMPT, stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            fputc('\n', stdout);
            break;
        } else {
            interpret(vm, line);
        }
    }
    return 0;
}

static char *read_file(const char *file_name) {
    FILE *handle = fopen(file_name, "rb");
    char *buffer = NULL;
    size_t file_size = 0;
    size_t bytes_read = 0;
    
    if (handle == NULL) {
        logprintfln("Failed to open file '%s'.", file_name);
        return NULL;
    }

    fseek(handle, 0L, SEEK_END);
    file_size = ftell(handle);
    rewind(handle);
    
    buffer = malloc(file_size + 1);
    if (buffer == NULL) {
        fclose(handle);
        logprintfln("Not enough memory to read '%s'.", file_name);
        return NULL;
    }

    bytes_read = fread(buffer, sizeof(char), file_size, handle);
    if (bytes_read < file_size) {
        free(buffer);
        fclose(handle);
        logprintfln("Could not read file '%s'.", file_name);
        return NULL;
    }

    buffer[bytes_read] = '\0';
    fclose(handle);
    return buffer;
}

static int run_file(VM *vm, const char *file_name) {
    char *input = read_file(file_name);
    InterpretResult res = interpret(vm, input);
    free(input);

    switch (res) {
    case INTERPRET_OK: 
        return 0;
    case INTERPRET_COMPILE_ERROR: 
        return EX_DATAERR;
    case INTERPRET_RUNTIME_ERROR: 
        return EX_SOFTWARE;
    default:
        return EXIT_FAILURE;
    }
}

int main(int argc, const char *argv[]) {
    VM *vm = &global_vm;
    int err = 0;

    if (argc == 1) {
        init_vm(vm, "stdin");
        err = repl(vm);
    } else if (argc == 2) {
        init_vm(vm, argv[1]);
        err = run_file(vm, argv[1]);
    } else {
        fprintf(stderr, "Usage: %s [script]\n", argv[0]);
        err = EX_USAGE;
    }
    free_vm(vm);
    return err;
}