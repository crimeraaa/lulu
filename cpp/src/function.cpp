#include "function.hpp"
#include "object.hpp"
#include "vm.hpp"

Closure *
closure_c_new(lulu_VM *vm, lulu_CFunction cf, int n_upvalues)
{
    Closure_C *f = object_new<Closure_C>(
        vm,
        &vm->objects,
        VALUE_FUNCTION,
        /* extra */ Closure_C::size_upvalues(n_upvalues)
    );

    f->n_upvalues = n_upvalues;
    f->is_c       = true;
    f->callback   = cf;
    return reinterpret_cast<Closure *>(f);
}

Closure *
closure_lua_new(lulu_VM *vm, Chunk *p)
{
    Closure_Lua *f = object_new<Closure_Lua>(vm, &vm->objects, VALUE_FUNCTION);
    f->n_upvalues  = 0;
    f->is_c        = false;
    f->chunk       = p;
    return reinterpret_cast<Closure *>(f);
}

void
closure_delete(lulu_VM *vm, Closure *f)
{
    if (f->is_c()) {
        Closure_C *cf = f->to_c();
        mem_free(vm, &f->c, cf->size_upvalues());
    } else {
        mem_free(vm, &f->lua);
    }
}
