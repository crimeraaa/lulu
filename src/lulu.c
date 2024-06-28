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
        size_t      len = strlen(line);
        lulu_Status err = lulu_load(vm, line, len, "stdin");
        if (err != LULU_OK) {
            printf("%s\n", lulu_to_string(vm, -1));
            lulu_pop(vm, 1);
        }
        // Allocation failures are unrecoverable.
        if (err == LULU_ERROR_ALLOC)
            return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static char *read_file(const char *name, size_t *out)
{
    FILE *f = fopen(name, "rb");
    if (f == nullptr) {
        logprintfln("Failed to open file '%s'.", name);
        return nullptr;
    }

    fseek(f, 0L, SEEK_END);
    size_t sz = ftell(f);
    rewind(f);

    char *buf = malloc(sz + 1);
    if (buf == nullptr) {
        fclose(f);
        logprintfln("Not enough memory to read '%s'.", name);
        return nullptr;
    }

    size_t read = fread(buf, sizeof(char), sz, f);
    if (read < sz) {
        free(buf);
        fclose(f);
        logprintfln("Could not read file '%s'.", name);
        return nullptr;
    }

    buf[read] = '\0';
    *out = read;
    fclose(f);
    return buf;
}

static int run_file(lulu_VM *vm, const char *name)
{
    size_t len;
    char  *input = read_file(name, &len);
    if (input == nullptr) {
        return EX_IOERR;
    }
    lulu_Status res = lulu_load(vm, input, len, name);
    free(input);
    if (res == LULU_OK)
        return EXIT_SUCCESS;

    printf("%s", lulu_to_string(vm, -1));
    lulu_pop(vm, 1);
    return EX_SOFTWARE;
}

int main(int argc, const char *argv[])
{
    lulu_VM *vm  = lulu_open();
    if (vm == nullptr) {
        eprintln("Failed to open lulu");
        return EXIT_FAILURE;
    } else if (argc != 1 && argc != 2) {
        eprintfln("Usage: %s [script]", argv[0]);
        return EX_USAGE;
    }
    int err = (argc == 1) ? repl(vm) : run_file(vm, argv[1]);
    lulu_close(vm);
    return err;
}
