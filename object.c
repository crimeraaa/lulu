#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

/* https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV_hash_parameters */
#define FNV32_OFFSET    0x811c9dc5
#define FNV32_PRIME     0x01000193

static Object *allocate_object(LVM *vm, size_t size, VType type) {
    Object *object = reallocate(NULL, 0, size);
    object->type = type;
    object->next = vm->objects; // Update the VM's allocation linked list
    vm->objects = object;
    return object;
}

#define allocate_object(vm, T, tag)     (T*)allocate_object(vm, sizeof(T), tag)

TFunction *new_function(LVM *vm) {
    TFunction *tf     = allocate_object(vm, TFunction, LUA_TFUNCTION);
    LFunction *luafn  = &tf->fn.lua;
    tf->is_c     = false;
    luafn->arity = 0;
    luafn->name  = NULL;
    init_chunk(&luafn->chunk);
    return tf;
}

TFunction *new_cfunction(LVM *vm, lua_CFunction function) {
    TFunction *tf = allocate_object(vm, TFunction, LUA_TFUNCTION);
    tf->is_c = true;
    tf->fn.c = function;
    return tf;
}

/**
 * III:19.2     Strings
 * 
 * Create a new TString on the heap and "take ownership" of the given buffer.
 * 
 * III:19.5     Freeing Objects
 * 
 * Because I *don't* want to use global state, we have to pass in a VM pointer!
 * But because this function can be called during the compile phase, the compiler
 * structure must also have a pointer to a VM...
 */
static inline TString *allocate_string(LVM *vm, char *data, size_t len, DWord hash) {
    TString *result = allocate_object(vm, TString, LUA_TSTRING);
    result->hash = hash;
    result->len  = len;
    result->data = data;
    // We don't care about the value for the interned strings, just the key.
    table_set(&vm->strings, result, makenil);
    return result;
}

/**
 * III:20.4.1   Hashing strings
 * 
 * This is the FNV-1A hash function which is pretty neat. It's not the most
 * cryptographically secure or whatnot, but it's something we can work with.
 * 
 * Unfortunately due to how we allocate the TString pointers, we have to
 * determine the hash AFTER writing the data pointer as we don't have access to
 * the full string in functions like `concat_strings()`.
 */
static DWord hash_string(const char *key, size_t len) {
    DWord hash = FNV32_OFFSET;
    for (size_t i = 0; i < len; i++) {
        hash ^= (Byte)key[i];
        hash *= FNV32_PRIME;
    }
    return hash;
}

TString *take_string(LVM *vm, char *data, size_t len) {
    DWord hash = hash_string(data, len);
    TString *interned = table_findstring(&vm->strings, data, len, hash);
    if (interned != NULL) {
        free(data);
        return interned;
    }
    return allocate_string(vm, data, len, hash);
}

TString *copy_string(LVM *vm, const char *literal, size_t len) {
    DWord hash = hash_string(literal, len);
    TString *interned = table_findstring(&vm->strings, literal, len, hash);
    if (interned != NULL) {
        return interned;
    }
    char *data = allocate(char, len + 1);
    memcpy(data, literal, len);
    data[len] = '\0';
    return allocate_string(vm, data, len, hash);
}

void print_function(const TFunction *self) {
    if (self->is_c) {
        printf("C function");
        return;
    }
    const LFunction *luafn = &self->fn.lua;
    if (luafn->name == NULL) {
        printf("<script>");
    } else {
        printf("function: %s", luafn->name->data);
    }
}

void print_string(const TString *self) {
    printf("%s", self->data);
}

#undef allocate_famobject
#undef allocate_famstring
