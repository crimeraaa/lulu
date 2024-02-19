#include "memory.h"

void *reallocate(void *pointer, size_t oldsize, size_t newsize) {
    (void)oldsize;
    if (newsize == 0) {
        free(pointer);
        return NULL;
    }
    void *result = realloc(pointer, newsize);
    // Not much we can do if this happens, so may as well let OS reclaim memory.
    if (result == NULL) {
        fprintf(stderr, "Failed to (re)allocate memory.\n");
        exit(EXIT_FAILURE);
    }
    return result;
}
