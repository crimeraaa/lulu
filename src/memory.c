#include "memory.h"
#include "object.h"
#include "vm.h"

void init_alloc(Alloc *self, ReallocFn reallocfn, void *context) {
    self->reallocfn = reallocfn;
    self->context   = context;
}

static void free_object(Object *object, Alloc *alloc) {
    switch (object->tag) {
    case TYPE_STRING:
        free_tstring(object, cast(TString*, object)->len + 1, alloc);
        break;
    default:
        break;
    }
}

void free_objects(VM *vm) {
    Alloc *alloc   = &vm->alloc;
    Object *object = vm->objects;

    while (object != NULL) {
        Object *next = object->next;
        free_object(object, alloc);
        object = next;
    }
}
