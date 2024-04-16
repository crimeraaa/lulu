#include "memory.h"
#include "object.h"
#include "vm.h"

void init_allocator(Allocator *self, AllocFn allocfn, FreeFn freefn, void *context) {
    self->allocfn = allocfn;
    self->freefn  = freefn;
    self->context = context;
}

static void free_object(Object *object, Allocator *allocator) {
    switch (object->tag) {
    case TYPE_STRING:
        free_tstring(object, cast(TString*, object)->len, allocator);
        break;
    default:
        break;
    }
}

void free_objects(VM *vm) {
    Allocator *allocator = &vm->allocator;
    Object *object       = vm->objects;

    while (object != NULL) {
        Object *next = object->next;
        free_object(object, allocator);
        object = next;
    }
}
