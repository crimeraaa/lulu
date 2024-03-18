#ifndef LUA_OBJECT_H
#define LUA_OBJECT_H

#include "common.h"
#include "chunk.h"
#include "value.h"

/**
 * III:24.7     Native Functions
 *
 * This is a function pointer type similar to the Lua C API protocol. In essence
 * all Lua-facing C functions MUST have an argument count and a pointer to the
 * first argument (i.e. pushed first) in the VM stack.
 *
 * NOTE:
 *
 * I've made it so that similar to the Lua C API, all C functions are able to
 * poke at their parent VM (the `lua_State*`). This is helpful to check if a
 * string has been interned, to intern new strings, etc.
 */
typedef TValue (*lua_CFunction)(LVM *vm, int argc);

struct LFunction {
    int arity;     // How many arguments are expected.
    Chunk chunk;   // Bytecode, constants and such.
    TString *name; // Interned identifier from source code.
};

struct TFunction {
    Object object; // Metadata/GC info.
    union {
        LFunction lua;   // Note how we use struct value itself, not pointer.
        lua_CFunction c; // C function pointer, nothing more and nothing less.
    } fn;                // We can be either a Lua function or a C function.
    bool is_c;           // Determine which member of the union to use.
};

struct TString {
    Object object; // Header for meta-information.
    DWord hash;    // Result of throwing `data` into a hash function.
    size_t len;    // Number of non-nul characters.
    char data[];   // C99-style flexible array member, a heap-allocated buffer.
};

Table *new_table(LVM *vm);

/**
 * III:24.1     Function Objects
 *
 * Set up a blank function: 0 arity, no name and no code. All we do is allocate
 * memory for an object of type tag `LUA_TFUNCTION` and append it to the VM's
 * objects linked list.
 * 
 * III:24.7     Native Functions
 * 
 * This now creates a tagged union struct `TFunction` with `is_c` set to false
 * and the appropriate values for a Lua function type `LFunction` defaulted.
 * It is to responsibility of the compiler to populate these values as needed.
 */
TFunction *new_function(LVM *vm);

/**
 * III:24.7     Native Functions
 * 
 * This allocates a new `TFunction` object, where the union tag `is_c` is set to
 * true and the `fn.c` union member is set to argument `function`.
 */
TFunction *new_cfunction(LVM *vm, lua_CFunction function);

/**
 * III:19.3     Strings
 *
 * Allocate enough memory to copy the string `data` byte for byte.
 * Currently, we don't have our hashtable implementation so we leak memory.
 *
 * III:19.5     Freeing Objects
 *
 * We take a pointer to the VM so we can update the objects linked list right.
 * It's a bit silly that we also needed to add a VM pointer member to the
 * Compiler struct, but it'll do since the VM is always initialized before the
 * compiler ever is.
 */
TString *copy_string(LVM *vm, const char *data, size_t len);

/**
 * Allocate enough memory to hold the sum of `lhs` and `rhs` lengths combined,
 * then fill the instance's buffer with the appropriate concatenated contents.
 * Then we check if the string is already interned.
 */
TString *concat_string(LVM *vm, const TString *lhs, const TString *rhs);

void print_function(const TFunction *self);
void print_string(const TString *self);

/* Given an `TValue*`, treat it as an `Object*` and get the type. */
#define objtype(v)          (asobject(v)->type)

#define asfunction(v)       ((TFunction*)asobject(v))
#define asluafunction(v)    (asfunction(v)->fn.lua)
#define ascfunction(v)      (asfunction(v)->fn.c)
#define asstring(v)         ((TString*)asobject(v))
#define ascstring(v)        (asstring(v)->data)
#define astable(v)          ((Table*)asobject(v))

#define isfunction(v)       isobject(LUA_TFUNCTION, v)
#define iscfunction(v)      (isfunction(v) && asfunction(v)->is_c)
#define isluafunction(v)    (isfunction(v) && !asfunction(v)->is_c)
#define isstring(v)         isobject(LUA_TSTRING, v)
#define istable(v)          isobject(LUA_TTABLE, v)

#define makestring(o)       makeobject(LUA_TSTRING, o)
#define makefunction(o)     makeobject(LUA_TFUNCTION, o)
#define maketable(o)        makeobject(LUA_TTABLE, o)

#endif /* LUA_OBJECT_H */
