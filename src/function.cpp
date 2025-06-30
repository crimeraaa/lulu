#include "function.hpp"
#include "object.hpp"
#include "vm.hpp"

Closure *
closure_new(lulu_VM *vm, lulu_CFunction cf)
{
    Closure_C *f = object_new<Closure_C>(vm, &vm->objects, VALUE_FUNCTION);
    f->is_c     = true;
    f->callback = cf;
    return cast(Closure *)f;
}

Closure *
closure_new(lulu_VM *vm, Chunk *c)
{
    Closure_Lua *f = object_new<Closure_Lua>(vm, &vm->objects, VALUE_FUNCTION);
    f->is_c     = false;
    f->chunk    = c;
    f->n_params = 0;
    return cast(Closure *)f;
}
