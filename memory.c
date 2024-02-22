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

/**
 * III:19.1:    Flexible Array Members (CHALLENGE)
 * 
 * Instead of separately allocating the object pointer and its char array,
 * we can use C99 flexible array members are just allocate the object pointer.
 * This requires us to be vigilant to NOT accidentally free the array itself,
 * because it's not a pointer!
 */
static void free_object(lua_Object *object) {
    switch (object->type) {
    case LUA_TSTRING:
        deallocate(lua_String, object);
        break;
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
