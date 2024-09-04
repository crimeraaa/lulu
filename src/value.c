#include "value.h"

void lulu_Value_Array_init(lulu_Value_Array *self)
{
    self->values = NULL;
    self->len    = 0;
    self->cap    = 0;
}

void lulu_Value_Array_write(lulu_Value_Array *self, const lulu_Value *v, const lulu_Allocator *a)
{
    if (self->len >= self->cap) {
        isize old_cap = self->cap;
        isize new_cap = GROW_CAPACITY(old_cap);
        self->values  = rawarray_resize(lulu_Value, self->values, old_cap, new_cap, a);
        self->cap     = new_cap;
    }
    self->values[self->len++] = *v;
}

void lulu_Value_Array_free(lulu_Value_Array *self, const lulu_Allocator *a)
{
    /**
     * @warning 2024-09-04
     *      Please ensure the type is correct!
     */
    rawarray_free(lulu_Value, self->values, self->cap, a);
    lulu_Value_Array_init(self);
}
