#ifdef LULU_DEBUG

#include <assert.h> // assert
#include <stdarg.h> // va_*
#include <stdlib.h> // abort
#include <stdio.h>  // [v]fprintf

// ASAN check with Clang,
#if (defined(__has_feature) && __has_feature(address_sanitizer))
#   ifndef __SANITIZE_ADDRESS__
#       define __SANITIZE_ADDRESS__ 1
#   endif
#endif

void
lulu_assert_fail(const char *where, const char *expr, const char *fmt,
    ...) throw()
{
    if (expr != nullptr) {
        fprintf(stderr, "%s: Assertion failed: %s\n", where, expr);
    } else {
        fprintf(stderr, "%s: Runtime panic\n", where);
    }
    if (fmt != nullptr) {
        va_list argp;
        va_start(argp, fmt);
        vfprintf(stderr, fmt, argp);
        va_end(argp);
    }

// GCC and MSVC already define the macro so we *should* be good here.
#ifdef __SANITIZE_ADDRESS__
    // intentionally cause segault to invoke ASAN for better backtrace
    *reinterpret_cast<volatile int *>(0) = 1;
#endif
    abort();
}

#endif // LULU_DEBUG
