#include "function.hpp"
#include "object.hpp"
#include "vm.hpp"

Function *
function_new(lulu_VM &vm, lulu_CFunction cf)
{
    CFunction *f = object_new<CFunction>(vm, &vm.objects, VALUE_FUNCTION);
    f->is_c     = true;
    f->callback = cf;
    return cast(Function *)f;
}

Function *
function_new(lulu_VM &vm, Chunk *c)
{
    LFunction *f = object_new<LFunction>(vm, &vm.objects, VALUE_FUNCTION);
    f->is_c     = false;
    f->chunk    = c;
    f->n_params = 0;
    return cast(Function *)f;
}
