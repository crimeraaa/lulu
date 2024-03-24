#include "memory.h"

void *reallocate(void *ptr, size_t oldsz, size_t newsz) {
    (void)oldsz; // Useful for custom allocators but malloc already bookkeeps.
    if (newsz == 0) {
        free(ptr);
        return NULL;
    }
    void *res = realloc(ptr, newsz);
    if (res == NULL) {
        exit(EXIT_FAILURE); // Likely there are bigger problems to worry about
    }
    return res;
}
