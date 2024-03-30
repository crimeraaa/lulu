#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "lua.h"
#include "opcodes.h"
#include "chunk.h"
#include "vm.h"

static int repl(lua_VM *vm) {
    char line[LUA_MAXINPUT];    
    for (;;) {
        printf(LUA_PROMPT);
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        interpret(vm, "stdin", line);
    }
    return EXIT_SUCCESS;
}

static char *read_file(const char *filename) {
    FILE *filehandle = fopen(filename, "rb");
    if (filehandle == NULL) {
        fprintf(stderr, "Could not open file '%s'.\n", filename);
        return NULL;
    }
    // Seek to end of file to determine size
    fseek(filehandle, 0L, SEEK_END);
    size_t filesize = ftell(filehandle);
    rewind(filehandle);

    // Allocate exactly enough memory for this file's contents as 1 big string
    char *buffer = malloc(filesize + 1);
    if (buffer == NULL) {
        fclose(filehandle);
        fprintf(stderr, "Not enough memory to read file '%s'.\n", filename);
        return NULL;
    }

    size_t bytesread = fread(buffer, sizeof(char), filesize, filehandle);
    if (bytesread < filesize) {
        fclose(filehandle);
        free(buffer);
        fprintf(stderr, "Could not read file '%s'.\n", filename);
        return NULL;
    }

    buffer[bytesread] = '\0';
    fclose(filehandle);
    return buffer;
}

static int run_file(lua_VM *vm, const char *filename) {
    char *contents = read_file(filename);
    if (contents == NULL) {
        return EX_IOERR;
    }
    InterpretResult result = interpret(vm, filename, contents);
    free(contents);
    
    switch (result) {
    case INTERPRET_OK:            return EXIT_SUCCESS;
    case INTERPRET_COMPILE_ERROR: return EX_DATAERR;
    case INTERPRET_RUNTIME_ERROR: return EX_SOFTWARE;
    }
}

static lua_VM global_vm = {0};

int main(int argc, const char *argv[]) {
    lua_VM *vm = &global_vm;
    int res = 0;
    init_vm(vm);
    if (argc == 1) {
        res = repl(vm);
    } else if (argc == 2) {
        res = run_file(vm, argv[1]);
    } else {
        fprintf(stderr, "Usage: %s [script]\n", argv[0]);
        res = EX_USAGE;
    }
    free_vm(vm);
    return res;
}
