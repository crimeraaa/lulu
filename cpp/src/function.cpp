#include "function.hpp"
#include "object.hpp"
#include "vm.hpp"

Closure *
closure_c_new(lulu_VM *L, lulu_CFunction cf, int n_upvalues)
{
    Closure_C *f = object_new<Closure_C>(L, &G(L)->objects, VALUE_FUNCTION,
        /*extra=*/Closure_C::size_upvalues(n_upvalues));

    f->n_upvalues = n_upvalues;
    f->is_c       = true;
    f->callback   = cf;
    return reinterpret_cast<Closure *>(f);
}

Closure *
closure_lua_new(lulu_VM *L, Chunk *p)
{
    int          n = p->n_upvalues;
    Closure_Lua *f = object_new<Closure_Lua>(L, &G(L)->objects, VALUE_FUNCTION,
        /*extra=*/Closure_Lua::size_upvalues(n));

    f->n_upvalues  = n;
    f->is_c        = false;
    f->chunk       = p;
    fill(f->slice_upvalues(), static_cast<Upvalue *>(nullptr));
    return reinterpret_cast<Closure *>(f);
}

void
closure_delete(lulu_VM *L, Closure *f)
{
    if (f->is_c()) {
        Closure_C *c = f->to_c();
        mem_free(L, c, c->size_upvalues());
    } else {
        Closure_Lua *lua = f->to_lua();
        mem_free(L, lua, lua->size_upvalues());
    }
}

Upvalue *
function_upvalue_find(lulu_VM *L, Value *local)
{
    Object *olist = L->open_upvalues;
    // Try to find and reuse an existing upvalue that references 'local'.
    while (olist != nullptr) {
        Upvalue *up = &olist->upvalue;

        // We assume the upvalue list is sorted (in terms of stack slots).
        // If we find one who is below the target, we must have gone past the
        // slot we want to close over meaning there is no upvalue for it.
        if (up->value < local) {
            break;
        }

        // Ensure this value is actually open.
        lulu_assert(up->value != &up->closed);

        // Found the upvalue we are looking for?
        if (up->value == local) {
            // Reuse it.
            return up;
        }
        olist = up->next;
    }

    // Couldn't find an upvalue; need to make a new one.
    // New upvalue is always open. Add it to the VM's open upvalue list.
    Upvalue *up = object_new<Upvalue>(L, &L->open_upvalues, VALUE_UPVALUE);

    // Current value lives on the stack. Closed is not yet used.
    up->value = local;
    return up;
}

static void
upvalue_link(lulu_VM *L, Upvalue *up)
{
    lulu_Global *g = G(L);
    up->next   = g->objects;
    g->objects = up->to_object();
    // @todo(2025-08-26) Check if object is collectible, etc.
}

void
function_upvalue_close(lulu_VM *L, Value *level)
{
    while (L->open_upvalues != nullptr) {
        Upvalue *up = &L->open_upvalues->upvalue;
        // Ensure we don't close upvalues that are already closed.
        lulu_assert(up->value != &up->closed);

        // We assume the upvalue list is sorted, this means we just went past
        // the slot we are closing over and thus there is no upvalue for it.
        if (up->value < level) {
            break;
        }

        L->open_upvalues = up->next;
        // TODO: check if object is dead
        // upvalue_unlink(up);

        // This upvalue is now closed over, so it owns its own value.
        up->closed = *up->value;
        up->value  = &up->closed;

        // Open upvalues were part of their object list; closed upvalues go
        // to the collectible side.
        upvalue_link(L, up);
    }
}
