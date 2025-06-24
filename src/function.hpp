#pragma once

#include "private.hpp"
#include "chunk.hpp"

#define FUNCTION_HEADER     OBJECT_HEADER; bool is_c

#define function_is_c(f)    ((f)->l.is_c)
#define function_is_lua(f)  (!function_is_c(f))

struct LFunction {
    FUNCTION_HEADER;
    Chunk *chunk;
    int    n_params;
};

struct CFunction {
    FUNCTION_HEADER;
    lulu_CFunction callback;
};

union Function {
    LFunction l;
    CFunction c;
};

Function *
function_new(lulu_VM &vm, lulu_CFunction cf);

Function *
function_new(lulu_VM &vm, Chunk *c);
