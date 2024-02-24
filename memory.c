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
        fprintf(stderr, "Failed to (re)allocate memory.\n");
        exit(EXIT_FAILURE);
    }
    return result;
}

static inline void free_string(lua_String *self) {
    deallocate_array(char, self->data, self->length);
    deallocate(lua_String, self);
}

static inline void free_object(lua_Object *self) {
    switch (self->type) {
    case LUA_TSTRING:
        free_string((lua_String*)self);
        break;
    }
}

void free_objects(lua_VM *lvm) {
    lua_Object *object = lvm->objects;
    while (object != NULL) {
        lua_Object *next = object->next;
        free_object(object);
        object = next;
    }    
}
