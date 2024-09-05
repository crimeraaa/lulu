#include "value.h"

void lulu_Value_Array_init(lulu_Value_Array *self)
{
    self->values = NULL;
    self->len    = 0;
    self->cap    = 0;
}

void lulu_Value_Array_write(lulu_VM *vm, lulu_Value_Array *self, const lulu_Value *v)
{
    if (self->len >= self->cap) {
        lulu_Value_Array_reserve(vm, self, GROW_CAPACITY(self->cap));
    }
    self->values[self->len++] = *v;
}

void lulu_Value_Array_reserve(lulu_VM *vm, lulu_Value_Array *self, isize new_cap)
{
    isize old_cap = self->cap;
    // Nothing to do?
    if (new_cap <= old_cap) {
        return;
    }
    
    self->values = rawarray_resize(lulu_Value, vm, self->values, old_cap, new_cap);
    self->cap    = new_cap;
}

void lulu_Value_Array_free(lulu_VM *vm, lulu_Value_Array *self)
{
    rawarray_free(lulu_Value, vm, self->values, self->cap);
    lulu_Value_Array_init(self);
}
