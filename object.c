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

static Object *_allocate_object(LVM *vm, size_t size, VType type) {
    Object *object = reallocate(NULL, 0, size);
    object->type = type;
    object->next = vm->objects; // Update the VM's allocation linked list
    vm->objects  = object;
    return object;
}

#define allocate_object(vm, T, tag)     (T*)_allocate_object(vm, sizeof(T), tag)

Table *new_table(LVM *vm) {
    Table *tbl = allocate_object(vm, Table, LUA_TTABLE);
    init_table(tbl);
    return tbl;
}

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

#define flexarraysize(T, M, n)  (sizeof(T) + (sizeof(M[n])))
#define sizeoftstring(n)        flexarraysize(TString, char, n + 1)
#define _allocate_string(vm, len) \
    (TString*)_allocate_object(vm, sizeoftstring(len), LUA_TSTRING)

/* We don't care about the value for the interned strings, just the key. */
static TString *intern_string(LVM *vm, TString *ts) {
    TValue key = makestring(ts);
    TValue val = makeboolean(true);
    table_set(&vm->strings, &key, &val);
    return ts;
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
static TString *allocate_string(LVM *vm, const char *data, size_t len, DWord hash) {
    TString *s = _allocate_string(vm, len);
    s->hash = hash;
    s->len  = len;
    memcpy(s->data, data, len);
    s->data[len] = '\0';
    return intern_string(vm, s);
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

TString *copy_string(LVM *vm, const char *data, size_t len) {
    DWord hash = hash_string(data, len);
    TString *interned = table_findstring(&vm->strings, data, len, hash);
    if (interned != NULL) {
        return interned;
    }
    return allocate_string(vm, data, len, hash);
}

static TString *allocate_string2(LVM *vm, const TString *lhs, const TString *rhs) {
    size_t len = lhs->len + rhs->len;
    TString *s = _allocate_string(vm, len);
    memcpy(&s->data[0],         lhs->data, lhs->len);
    memcpy(&s->data[lhs->len],  rhs->data, rhs->len);
    s->data[len] = '\0';
    s->len  = len;
    s->hash = hash_string(s->data, len);
    return s;
}

TString *concat_string(LVM *vm, const TString *lhs, const TString *rhs) {
    TString *s        = allocate_string2(vm, lhs, rhs);
    TString *interned = table_findstring(&vm->strings, s->data, s->len, s->hash);
    if (interned != NULL) {
        vm->objects = s->object.next; // Remove from allocations linked list
        deallocate(TString, s);       // NOTE: Does not include flexarray size.
        return interned;
    }
    return intern_string(vm, s);
}

void print_function(const TFunction *self) {
    if (self->is_c) {
        printf("C function: %p", (void*)self);
        return;
    }
    if (self->fn.lua.name == NULL) {
        printf("<script>");
    } else {
        printf("function: %p", (void*)self);
    }
}

void print_string(const TString *self) {
    printf("%s", self->data);
}
