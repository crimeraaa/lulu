#pragma once

#include "chunk.hpp"
#include "private.hpp"

// Do not create stack-allocated instances of these; unaligned accesses may occur.
struct [[gnu::packed]] Closure_Header : Object_Header {
    u8 n_upvalues;
    bool is_c;
};

struct Upvalue : Object_Header {
    // Points to value in stack while open, else points to `closed`.
    Value *value;
    Value closed = nil;
};

struct Closure_Lua : Closure_Header {
    Chunk   *chunk;
    Upvalue *upvalues[1];

    static isize
    size_upvalues(int n)
    {
        return size_of(upvalues[0]) * static_cast<isize>(n - 1); // NOLINT
    }

    isize
    size_upvalues() const noexcept
    {
        return Closure_Lua::size_upvalues(this->n_upvalues);
    }

    Slice<Upvalue *>
    slice_upvalues() noexcept
    {
        return {this->upvalues, this->n_upvalues};
    }
};

struct Closure_C : Closure_Header {
    lulu_CFunction callback;
    Value          upvalues[1];

    // If `n_upvalues == 0`, then `upvalues[0]` should not be valid.
    // So negative sizes are allowed.
    static isize
    size_upvalues(int n)
    {
        return size_of(upvalues[0]) * static_cast<isize>(n - 1);
    }

    isize
    size_upvalues() const noexcept
    {
        return Closure_C::size_upvalues(this->n_upvalues);
    }

    Slice<Value>
    slice_upvalues() noexcept
    {
        return {this->upvalues, this->n_upvalues};
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
closure_c_new(lulu_VM *L, lulu_CFunction cf, int n_upvalues);

Closure *
closure_lua_new(lulu_VM *L, Chunk *p);

void
closure_delete(lulu_VM *L, Closure *f);

void
function_upvalue_close(lulu_VM *L, Value *level);

Upvalue *
function_upvalue_find(lulu_VM *L, Value *local);
