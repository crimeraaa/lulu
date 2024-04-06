#include "memory.h"

void *reallocate(void *ptr, size_t oldsz, size_t newsz) {
    // May be useful for custom allocators, however C standard allocators do the
    // book-keeping for us already so we can afford to ignore it here.
    unused(oldsz);
    if (newsz == 0) {
        free(ptr);
        return NULL;
    } else {
        void *res = realloc(ptr, newsz);
        if (res == NULL) {
            logprintln("Failed to allocate memory.");
            exit(EXIT_FAILURE); // Not much else can be done in this case.
        }
        return res;
    }
}
