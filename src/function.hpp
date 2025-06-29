#pragma once

#include "private.hpp"
#include "chunk.hpp"

#define CLOSURE_HEADER     OBJECT_HEADER; bool is_c

#define closure_is_c(f)    ((f)->l.is_c)
#define closure_is_lua(f)  (!closure_is_c(f))

struct Closure_Lua {
    CLOSURE_HEADER;
    Chunk *chunk;
    int    n_params;
};

struct Closure_C {
    CLOSURE_HEADER;
    lulu_CFunction callback;
};

union Closure {
    Closure_Lua l;
    Closure_C   c;
};

Closure *
closure_new(lulu_VM &vm, lulu_CFunction cf);

Closure *
closure_new(lulu_VM &vm, Chunk *c);
