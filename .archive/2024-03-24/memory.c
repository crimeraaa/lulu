#include "memory.h"
#include "object.h"
#include "table.h"
#include "vm.h"

void *reallocate(void *pointer, size_t oldsize, size_t newsize) {
    unused(oldsize);
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
    // deallocate_array(char, self->data, self->len);
    deallocate(TString, self); // NOTE: Does not include flexarray member size.
}

static void free_function(Proto *self) {
    // C Functions don't have chunks, so don't free that part of the union.
    if (!self->is_c) {
        free_chunk(&self->fn.lua.chunk);
    }
    deallocate(Proto, self);
}

// Don't reinit and also need to free the pointer itself.
static void free_table2(Table *self) {
    deallocate_array(Entry, self->entries, self->cap);
    deallocate(Table, self);
}

static void free_object(Object *self) {
    switch (self->type) {
    case LUA_TFUNCTION: free_function((Proto*)self); break;
    case LUA_TSTRING:   free_string((TString*)self);     break;
    case LUA_TTABLE:    free_table2((Table*)self);       break;
    default:            return;
    }
}

void free_objects(LVM *vm) {
    printf("\n");
    Object *object = vm->objects;
    while (object != NULL) {
        Object *next = object->next;
        free_object(object);
        object = next;
    }    
}
