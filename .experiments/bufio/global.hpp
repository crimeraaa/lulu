#pragma once

#include "mem.hpp"

struct Object;

struct Value {
    enum class Type {
        String,
        Number,
    };
    union Data {
        double number;
        Object *object;
    };
    
    Data data;
    Type type;
};

struct Object {
    using Type = Value::Type;

    Object *next; // Linked list.
    Type    type;
};

// Global state.
struct Global {
    Allocator allocator;
    Object   *objects;
};

void init_global(Global *g);
