#include <stdio.h>  /* FILE*, stdin, printf(family), fgets, fopen, fclose */
#include <stdlib.h> /* malloc, free */
#include <string.h> /* strcspn */
#include "common.h"
#include "conf.h"
#include "chunk.h"
#include "vm.h"

#if defined(unix) || defined(__unix__) || defined(__unix)
#include <sysexits.h>
#else /* unix not defined. */
#define EX_USAGE    64 /* Command line usage error. */
#define EX_DATAERR  65 /* Data format error. */
#define EX_NOINPUT  66 /* Cannot open input. */
#define EX_SOFTWARE 70 /* Internal software error. */
#define EX_IOERR    74 /* Input/output error. */
#endif /* unix */

static void run_repl(lua_VM *vm) {
    char line[LUA_REPL_BUFSIZE];
    for (;;) {
        printf("> ");
        // Appends newline and nul char
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        interpret_vm(vm, line);
    } 
}

static char *read_file(const char *file_path) {
    FILE *handle = fopen(file_path, "rb"); // read only in binary mode
    if (handle == NULL) {
        logprintf("Could not open file '%s'.\n", file_path);
        exit(EX_IOERR);
    }
    fseek(handle, 0L, SEEK_END);
    size_t file_size = ftell(handle);
    rewind(handle); // Brings internal file pointer back to beginning
    
    char *buffer = malloc(file_size + 1);
    if (buffer == NULL) {
        logprintf("Not enough memory to read file '%s'.\n", file_path);
        exit(EX_IOERR);
    }
    size_t bytes_read = fread(buffer, sizeof(char), file_size, handle);
    if (bytes_read < file_size) {
        logprintf("Could not read file '%s'.\n", file_path);
    }
    buffer[bytes_read] = '\0';
    fclose(handle);
    return buffer;
}

static int run_file(lua_VM *vm, const char *file_path) {
    char *source = read_file(file_path);
    InterpretResult result = interpret_vm(vm, source);
    free(source);

    switch (result) {
    case INTERPRET_OK: break;
    case INTERPRET_COMPILE_ERROR: return EX_DATAERR;
    case INTERPRET_RUNTIME_ERROR: return EX_SOFTWARE;
    default: // This should not happen, but just in case
        logprintf("Unknown result code %i.\n", (int)result); 
        break;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    lua_VM *vm = &(lua_VM){0}; // C99 compound literals are really handy sometimes
    init_vm(vm);
    int retval = 0; 
    if (argc == 1) {
        run_repl(vm);
    } else if (argc == 2) {
        retval = run_file(vm, argv[1]);
    } else {
        fprintf(stderr, "Usage: %s [script]\n", argv[0]);
        retval = EX_USAGE;
    }
    free_vm(vm);
    return retval;
}
