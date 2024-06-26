#pragma once

#include "mem.hpp"

union Object;

struct Value {
    enum class Type {
        Nil,
        Boolean,
        Number,
        String,
        Table,
    }; 

    union Data {
        bool    boolean;
        Number  number;
        Object *object;
    };
    
    Type type;
    Data data;
};

// --- OBJECTS ------------------------------------------------------------ {{{1

// Must be common across ALL object types.
#define CommonObjectHeader \
    using Type = Value::Type; \
    Object *next; /* Linked list. */ \
    Type    type


// Fields thereof MUST be present in ALL object types.
struct Base {CommonObjectHeader;};

// --- STRING ------------------------------------------------------------- {{{2

struct String {
    using Hash = uint64_t;

    CommonObjectHeader;
    char  *data;   // Null terminated C string.
    Hash   hash;   // FNV1A hash.
    size_t length; // Number of characters, sans nul.
};

// --- 2}}} --------------------------------------------------------------------

// --- TABLE -------------------------------------------------------------- {{{2

static constexpr double TABLE_MAX_LOAD = 0.75;

struct Table {
    struct Pair {
        Value key;
        Value val;
    };
    
    CommonObjectHeader;
    Pair  *pairs;    // Associative array segment.
    size_t count;    // Number of currently active entries.
    size_t capacity; // Number of entries can hold in total.
};

// --- 2}}} --------------------------------------------------------------------


// As long as ALL of them have the same first fields, this should be fine.
// See: https://www.lua.org/source/5.1/lstate.h.html#GCObject
union Object {
    Base   base;
    String string;
    Table  table;
};

#undef CommonObjectHeader

// --- 1}}} --------------------------------------------------------------------

#define cast_pointer(expr)  (reinterpret_cast<void*>(expr))
#define cast_object(expr)   (reinterpret_cast<Object*>(expr))
#define cast_string(expr)   (reinterpret_cast<String*>(expr))
#define cast_table(expr)    (reinterpret_cast<Table*>(expr))

#define get_type(v)     ((v)->type)
#define is_type(v, t)   (get_type(v) == t)
#define is_nil(v)       is_type(v, Value::Type::Nil)
#define is_boolean(v)   is_type(v, Value::Type::Boolean)
#define is_number(v)    is_type(v, Value::Type::Number)
#define is_string(v)    is_type(v, Value::Type::String) 
#define is_table(v)     is_type(v, Value::Type::Table)

#define as_boolean(v)   ((v)->data.boolean)
#define as_number(v)    ((v)->data.number)
#define as_object(v)    ((v)->data.object)
#define as_string(v)    cast_string(as_object(v))
#define as_table(v)     cast_table(as_object(v))
#define as_pointer(v)   cast_pointer(as_object(v))

// Global state.
struct Global {
    Allocator allocator;
    Object   *objects;
    Table    *strings;
};

// Assumes `buf` is a stack-allocated array of size `MAX_TO_CSTRING`.
const char *to_cstring_value(const Value *v, char *buf);
Value nil_value();
Value boolean_value(bool b);
Value number_value(Number n);
Value string_value(String *s);
Value table_value(Table *t);

void init_global(Global *g);
void free_global(Global *g);

template<class T>
T *new_object(Global *g, Value::Type t, size_t extra = 0);

String  *new_string(Global *g, const char *cs, size_t len, String::Hash hash);
void     free_string(Global *g, String *s);
String  *copy_string(Global *g, const char *cs, size_t len);
void     intern_string(Global *g, String *s);
String  *lookup_string(Global *g, const char *cs, size_t len, String::Hash hash);

Table *new_table(Global *g, size_t n = 0);
void   free_table(Global *g, Table *t);
Value *get_table(Table *t, const Value *k); // ltable.c:luaH_get
Value *set_table(Global *g, Table *t, const Value *k, const Value *v); // ltable.c:luaH_set
