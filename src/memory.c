#include "memory.h"
#include "vm.h"

void *lulu_Allocator_alloc(lulu_VM *vm, isize new_size)
{
    return vm->allocator(
        vm->allocator_data,
        LULU_ALLOCATOR_MODE_ALLOC,
        new_size,
        LULU_ALLOCATOR_ALIGNMENT,
        NULL,
        0); 
}

void *lulu_Allocator_resize(lulu_VM *vm, void *old_ptr, isize old_size, isize new_size)
{
    return vm->allocator(
        vm->allocator_data,
        LULU_ALLOCATOR_MODE_RESIZE,
        new_size,
        LULU_ALLOCATOR_ALIGNMENT,
        old_ptr,
        old_size);
}

void lulu_Allocator_free(lulu_VM *vm, void *old_ptr, isize old_size)
{
    vm->allocator(
        vm->allocator_data,
        LULU_ALLOCATOR_MODE_FREE,
        0,
        LULU_ALLOCATOR_ALIGNMENT,
        old_ptr,
        old_size);
}
