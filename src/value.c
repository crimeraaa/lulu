#include "value.h"

void lulu_Value_Array_init(lulu_Value_Array *self, const lulu_Allocator *allocator)
{
    self->allocator = *allocator;
    self->values = NULL;
    self->len    = 0;
    self->cap    = 0;
}

void lulu_Value_Array_write(lulu_Value_Array *self, const lulu_Value *v)
{
    if (self->len >= self->cap) {
        lulu_Value_Array_reserve(self, GROW_CAPACITY(self->cap));
    }
    self->values[self->len++] = *v;
}

void lulu_Value_Array_reserve(lulu_Value_Array *self, isize new_cap)
{
    const lulu_Allocator *allocator = &self->allocator;
    isize old_cap = self->cap;
    // Nothing to do?
    if (new_cap <= old_cap) {
        return;
    }
    
    self->values = rawarray_resize(lulu_Value, self->values, old_cap, new_cap, allocator);
    self->cap    = new_cap;
}

void lulu_Value_Array_free(lulu_Value_Array *self)
{
    const lulu_Allocator *allocator = &self->allocator;
    /**
     * @warning 2024-09-04
     *      Please ensure the type is correct!
     */
    rawarray_free(lulu_Value, self->values, self->cap, allocator);
    lulu_Value_Array_init(self, allocator);
}
