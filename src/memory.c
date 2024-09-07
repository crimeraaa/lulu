#include "memory.h"
#include "vm.h"

void *lulu_Allocator_alloc(lulu_VM *vm, isize new_size)
{
    void *new_ptr = vm->allocator(vm->allocator_data, new_size, LULU_ALLOCATOR_ALIGNMENT, NULL, 0); 
    if (!new_ptr) {
        lulu_VM_throw_error(vm, LULU_ERROR_MEMORY);
    }
    return new_ptr;
}

void *lulu_Allocator_resize(lulu_VM *vm, void *old_ptr, isize old_size, isize new_size)
{
    void *new_ptr = vm->allocator(vm->allocator_data, new_size, LULU_ALLOCATOR_ALIGNMENT, old_ptr, old_size);
    if (!new_ptr) {
        lulu_VM_throw_error(vm, LULU_ERROR_MEMORY);
    }
    return new_ptr;
}

void lulu_Allocator_free(lulu_VM *vm, void *old_ptr, isize old_size)
{
    vm->allocator(vm->allocator_data, 0, LULU_ALLOCATOR_ALIGNMENT, old_ptr, old_size);
}
