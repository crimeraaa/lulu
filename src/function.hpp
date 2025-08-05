#pragma once

#include "private.hpp"
#include "chunk.hpp"

#define CLOSURE_HEADER     OBJECT_HEADER; int n_upvalues; bool is_c

struct LULU_PRIVATE Closure_Lua {
    CLOSURE_HEADER;
    Chunk *chunk;
};

struct LULU_PRIVATE Closure_C {
    CLOSURE_HEADER;
    lulu_C_Function callback;
    Value upvalues[1];

    // If `n_upvalues == 0`, then `upvalues[0]` should not be valid.
    // So negative sizes are allowed.
    static isize
    size_upvalues(isize n)
    {
        return sizeof(upvalues[0]) * (n - 1);
    }
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

    Closure_Lua *
    to_lua()
    {
        lulu_assert(this->is_lua());
        return &this->lua;
    }

    Closure_C *
    to_c()
    {
        lulu_assert(this->is_c());
        return &this->c;
    }
};

LULU_FUNC Closure *
closure_c_new(lulu_VM *vm, lulu_C_Function cf, int n_upvalues);

LULU_FUNC Closure *
closure_lua_new(lulu_VM *vm, Chunk *p);
