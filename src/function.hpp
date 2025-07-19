#pragma once

#include "private.hpp"
#include "chunk.hpp"

#define CLOSURE_HEADER     OBJECT_HEADER; bool is_c

#define closure_is_c(f)    ((f)->lua.is_c)
#define closure_is_lua(f)  (!closure_is_c(f))

struct LULU_PRIVATE Closure_Lua {
    CLOSURE_HEADER;
    Chunk *chunk;
    int    n_params;
};

struct LULU_PRIVATE Closure_C {
    CLOSURE_HEADER;
    lulu_CFunction callback;
};

union LULU_PRIVATE Closure {
    Closure_Lua lua;
    Closure_C   c;
};

LULU_FUNC Closure *
closure_new(lulu_VM *vm, lulu_CFunction cf);

LULU_FUNC Closure *
closure_new(lulu_VM *vm, Chunk *c);
