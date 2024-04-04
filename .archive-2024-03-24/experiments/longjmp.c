#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

#define xtostring(x)        #x
#define stringify(x)        xtostring(x)
#define logstring()         __FILE__ ":" stringify(__LINE__) ": "
#define logprintln(s)       fputs(logstring() s "\n", stderr)
#define logprintf(fmt, ...) fprintf(stderr, logstring() fmt, __VA_ARGS__)

jmp_buf nonlocaljmp;

void somerecursion(int n) {
    logprintf("somerecursion() says '%i'\n", n);
    if (n >= 4) {
        logprintln("somerecursion() signaled longjmp");
        longjmp(nonlocaljmp, 1);
    }
    somerecursion(n + 1);
}

void somefunction() {
    logprintln("somefunction()");
    somerecursion(0);
}

int main() {
    logprintln("main()");
    // 0 is returned when it's first set, otherwise nonzero when longjmp'd.
    if (setjmp(nonlocaljmp) != 0) {
        logprintln("main() received longjmp");
    } else {
        // Keep this in a separate branch otherwise we'll infinite loop, and I
        // believe it would be undefined behaviour anyway.
        somefunction();
    }
    return 0;
}
