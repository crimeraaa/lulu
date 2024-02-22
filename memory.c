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

static void free_object(lua_Object *object) {
    switch (object->type) {
    case LUA_TSTRING: {
        lua_String *s = (lua_String*)object;
        deallocate_array(char, s->data, s->length + 1);
        deallocate(lua_String, object);
        break;
    } 
    }
}

void free_objects(LuaVM *lvm) {
    lua_Object *object = lvm->objects;
    while (object != NULL) {
        lua_Object *next = object->next;
        free_object(object);
        object = next;
    }    
}
