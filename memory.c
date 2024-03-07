#include "memory.h"
#include "object.h"
#include "vm.h"

void *reallocate(void *pointer, size_t oldsize, size_t newsize) {
    (void)oldsize;
    if (newsize == 0) {
        free(pointer);
        return NULL;
    }
    void *result = realloc(pointer, newsize);
    // Not much we can do if this happens, so may as well let OS reclaim memory.
    if (result == NULL) {
        logprintln("Failed to (re)allocate memory.");
        exit(EXIT_FAILURE);
    }
    return result;
}

static void free_string(TString *self) {
    deallocate_array(char, self->data, self->len);
    deallocate(TString, self);
}

static void free_function(LFunction *self) {
    free_chunk(&self->chunk);
    deallocate(LFunction, self);
}

static void free_object(Object *self) {
    switch (self->type) {
    case LUA_TFUNCTION: free_function((LFunction*)self); break;
    case LUA_TNATIVE:   deallocate(CFunction, self); break;
    case LUA_TSTRING:   free_string((TString*)self); break;
    default:            return;
    }
}

void free_objects(LVM *vm) {
    Object *object = vm->objects;
    while (object != NULL) {
        Object *next = object->next;
        free_object(object);
        object = next;
    }    
}
