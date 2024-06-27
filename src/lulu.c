#include "api.h"
#include "lulu.h"
#include "limits.h"

#include <sysexits.h>

static int repl(lulu_VM *vm)
{
    char line[LULU_MAX_LINE];
    for (;;) {
        fputs(LULU_PROMPT, stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            fputc('\n', stdout);
            break;
        }
        lulu_Status err = lulu_interpret(vm, "=stdin", line);
        if (err != LULU_OK) {
            printf("%s\n", lulu_to_cstring(vm, -1));
            lulu_pop(vm, 1);
        }
        // Allocation failures are unrecoverable.
        if (err == LULU_ERROR_ALLOC)
            return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static char *read_file(const char *file_name)
{
    FILE   *handle     = fopen(file_name, "rb");
    char   *buffer     = NULL;
    size_t  file_size  = 0;
    size_t  bytes_read = 0;

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

static int run_file(lulu_VM *vm, const char *file_name)
{
    char *input = read_file(file_name);
    if (input == NULL) {
        return EX_IOERR;
    }
    lulu_Status res = lulu_interpret(vm, file_name, input);
    free(input);
    if (res == LULU_OK)
        return EXIT_SUCCESS;

    printf("%s", lulu_to_cstring(vm, -1));
    lulu_pop(vm, 1);
    return EX_SOFTWARE;
}

int main(int argc, const char *argv[])
{
    lulu_VM *vm  = lulu_open();
    int      err = 0;
    if (vm == NULL) {
        eprintln("Failed to open lulu");
        return EXIT_FAILURE;
    }
    if (argc == 1) {
        err = repl(vm);
    } else if (argc == 2) {
        err = run_file(vm, argv[1]);
    } else {
        eprintfln("Usage: %s [script]", argv[0]);
        return EX_USAGE;
    }
    lulu_close(vm);
    return err;
}
