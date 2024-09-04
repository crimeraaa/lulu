#include "memory.h"

void *lulu_Allocator_alloc(const lulu_Allocator *self, isize new_size, isize align)
{
    return self->procedure(self->data,
        LULU_ALLOCATOR_MODE_ALLOC, new_size, align, NULL, 0); 
}

void *lulu_Allocator_resize(const lulu_Allocator *self, void *old_ptr, isize old_size, isize new_size, isize align)
{
    return self->procedure(self->data,
        LULU_ALLOCATOR_MODE_RESIZE, new_size, align, old_ptr, old_size);
}

void lulu_Allocator_free(const lulu_Allocator *self, void *old_ptr, isize old_size)
{
    self->procedure(self->data,
        LULU_ALLOCATOR_MODE_FREE, 0, 0, old_ptr, old_size);
}
