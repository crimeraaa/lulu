#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Using C99 flexible-array members, we can allocate the entire structure in
 * one fell swoop 
 * 
 * That is, instead of allocating for a `cri_String*` and `char*`, we can allocate
 * only once.
 * 
 */
typedef struct {
    Size len;
    Size cap;
    char data[];
} cri_String;

void *reallocate(void *ptr, Size oldsize, Size newsize) {
    // Unused as C standard malloc does bookkeeping, but maybe for custom
    // allocators this parameter will be very helpful.
    (void)oldsize; 
    if (newsize == 0) {
        free(ptr);
        return NULL;
    }
    void *res = realloc(ptr, newsize);
    if (res == NULL) {
        fprintf(stderr, "F\n");
        exit(EXIT_FAILURE);
    }
    return res;
}

#define allocate(size)          reallocate(NULL, 0, size)
#define deallocate(ptr, size)   reallocate(ptr, size, 0)

cri_String *make_string(const char *src, Size len) {
    cri_String *inst = allocate(sizeof(cri_String) + sizeof(char[len + 1]));
    inst->len = len;
    inst->cap = len + 1;
    memcpy(inst->data, src, len);
    inst->data[len] = '\0';
    return inst;
}

void free_string(cri_String *self) {
    deallocate(self, sizeof(char[self->cap]));
}

int main(void) {
    const char *greet = "Hi mom!";
    cri_String *thing = make_string(greet, strlen(greet));
    // printf("thing=%s{len=%zu,cap=%zu}\n", thing->data, thing->len, thing->cap);
    free_string(thing);
    return 0;
}
