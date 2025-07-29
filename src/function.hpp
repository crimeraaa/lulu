#pragma once

#include "private.hpp"
#include "chunk.hpp"

#define CLOSURE_HEADER     OBJECT_HEADER; bool is_c

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

    bool
    is_c() const noexcept
    {
        return this->lua.is_c;
    }

    bool
    is_lua() const noexcept
    {
        return !this->lua.is_c;
    }

    const Closure_Lua *
    to_lua() const
    {
        lulu_assert(this->is_lua());
        return &this->lua;
    }

    const Closure_C *
    to_c() const
    {
        lulu_assert(this->is_c());
        return &this->c;
    }
};

LULU_FUNC Closure *
closure_c_new(lulu_VM *vm, lulu_CFunction cf);

LULU_FUNC Closure *
closure_lua_new(lulu_VM *vm, Chunk *p);
