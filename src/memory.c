#include "memory.h"

#ifndef LULU_NOSTDLIB

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void *lulu_heap_allocator_proc(
    void *              allocator_data,
    lulu_Allocator_Mode mode,
    isize               new_size,
    isize               align,
    void *              old_ptr,
    isize               old_size)
{
    void *new_ptr = NULL;
    isize add_len = new_size - old_size;

    unused(allocator_data);
    unused(align);

    switch (mode) {
    case LULU_ALLOCATOR_MODE_ALLOC: // fall through
    case LULU_ALLOCATOR_MODE_RESIZE:
        new_ptr = realloc(old_ptr, new_size);
        if (!new_ptr) {
            fprintf(stderr, "[FATAL]: %s\n", "[Re]allocation failure!");
            fflush(stderr);
            abort();
        }
        // We extended the allocation? Note that immediately loading a possibly
        // invalid pointer is not a safe assumption for 100% of architectures.
        if (add_len > 0) {
            byte *add_ptr = cast(byte *)new_ptr + old_size;
            memset(add_ptr, 0, add_len);
        }
        break;
    case LULU_ALLOCATOR_MODE_FREE:
        free(old_ptr);
        break;
    }
    return new_ptr;
}

const lulu_Allocator lulu_heap_allocator = {
    &lulu_heap_allocator_proc,
    NULL,
};

#endif // LULU_NOSTDLIB

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
