#ifdef LULU_DEBUG

#include <execinfo.h> // backtrace, backtrace_symbols_fd
#include <unistd.h>   // STD*_FILENO
#include <stdio.h>    // fprintf
#include <stdlib.h>

#include "private.h"

#undef lulu_assert

void lulu_assert(const char *file, const char *func, int line, bool cond, const char *expr)
{
    if (!cond) {
        void *buffer[16];
        int n = backtrace(buffer, count_of(buffer));
        fprintf(stderr, "%s:%s:%i: Assertion failed: %s\n", file, func, line, expr);

        // skip `backtrace()` and `lulu_assert`
        backtrace_symbols_fd(&buffer[2], n - 2, STDERR_FILENO);
        __builtin_trap();
    }
}

#endif // LULU_DEBUG
