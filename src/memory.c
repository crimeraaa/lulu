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
    case TYPE_TABLE:
        free_table(cast(Table*, object), alloc);
        free_pointer(Table, object, alloc);
        break;
    default:
        eprintfln("[FATAL ERROR]:\nAttempt to free a %s", get_typename(object));
        assert(false);
        break;
    }
}

void free_objects(VM *vm) {
    Alloc  *alloc = &vm->alloc;
    Object *head  = vm->objects;
    while (head != NULL) {
        Object *next = head->next;
        free_object(head, alloc);
        head = next;
    }
}
