#include "memory.h"
#include "object.h"
#include "vm.h"

void *reallocate(VM *vm, void *ptr, size_t oldsz, size_t newsz) {
    // May be useful for custom allocators, however C standard allocators do the
    // book-keeping for us already so we can afford to ignore it here.
    unused(oldsz);
    if (newsz == 0) {
        free(ptr);
        return NULL;
    }
    void *res = realloc(ptr, newsz);
    if (res == NULL) {
        logprintln("[FATAL ERROR]: No more memory.");
        longjmp(vm->errorjmp, ERROR_ALLOC);
    }
    return res;
}

static void free_object(VM *vm, Object *object) {
    switch (object->tag) {
    case TYPE_STRING:
        deallocate_tstring(vm, cast(TString*, object));
        break;
    default:
        break;
    }
}

void free_objects(VM *vm) {
    Object *object = vm->objects;
    while (object != NULL) {
        Object *next = object->next;
        free_object(vm, object);
        object = next;
    }
}
