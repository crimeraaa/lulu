#pragma once

#include "chunk.hpp"
#include "private.hpp"

#define CLOSURE_HEADER                                                         \
    OBJECT_HEADER;                                                             \
    int  n_upvalues;                                                           \
    bool is_c

struct Closure_Lua {
    CLOSURE_HEADER;
    Chunk *chunk;
};

struct Closure_C {
    CLOSURE_HEADER;
    lulu_CFunction callback;
    Value          upvalues[1];

    // If `n_upvalues == 0`, then `upvalues[0]` should not be valid.
    // So negative sizes are allowed.
    static isize
    size_upvalues(isize n)
    {
        return sizeof(upvalues[0]) * (n - 1);
    }

    isize
    size_upvalues() const noexcept
    {
        return Closure_C::size_upvalues(this->n_upvalues);
    }
};

union Closure {
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

Closure *
closure_c_new(lulu_VM *vm, lulu_CFunction cf, int n_upvalues);

Closure *
closure_lua_new(lulu_VM *vm, Chunk *p);

void
closure_delete(lulu_VM *vm, Closure *f);
