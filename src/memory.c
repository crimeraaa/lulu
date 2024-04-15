#include "memory.h"
#include "object.h"
#include "vm.h"

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

#define deallocate_flexarray(ST, MT, N, ptr) \
    reallocate(ptr, flexarray_size(ST, MT, N), 0)

// +1 was allocated for the nul char.
#define deallocate_tstring(ptr) \
    deallocate_flexarray(TString, char, cast(TString*, ptr)->len + 1, ptr)

static void free_object(Object *object) {
    switch (object->tag) {
    case TYPE_STRING:
        deallocate_tstring(object);
        break;
    default:
        break;
    }
}

void free_objects(VM *vm) {
    Object *object = vm->objects;
    while (object != NULL) {
        Object *next = object->next;
        free_object(object);
        object = next;
    }
}
