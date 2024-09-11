#include "memory.h"
#include "vm.h"
#include "debug.h"

static void *
wrap_alloc(lulu_VM *vm, void *new_ptr, isize new_size)
{
    if (!new_ptr) {
        lulu_Debug_fatalf("Out of memory (failed to allocate %ti bytes)", new_size);
        lulu_VM_throw_error(vm, LULU_ERROR_MEMORY);
    }
    return new_ptr;
}

void *
lulu_Memory_alloc(lulu_VM *vm, isize new_size)
{
    void *new_ptr = vm->allocator(vm->allocator_data, new_size, LULU_ALLOCATOR_ALIGNMENT, NULL, 0); 
    return wrap_alloc(vm, new_ptr, new_size);
}

void *
lulu_Memory_resize(lulu_VM *vm, void *old_ptr, isize old_size, isize new_size)
{
    void *new_ptr = vm->allocator(vm->allocator_data, new_size, LULU_ALLOCATOR_ALIGNMENT, old_ptr, old_size);
    return wrap_alloc(vm, new_ptr, new_size);
}

void
lulu_Memory_free(lulu_VM *vm, void *old_ptr, isize old_size)
{
    vm->allocator(vm->allocator_data, 0, LULU_ALLOCATOR_ALIGNMENT, old_ptr, old_size);
}
