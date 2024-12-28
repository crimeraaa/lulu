/// local
#include "memory.h"
#include "vm.h"
#include "debug.h"

static void *
wrap_alloc(lulu_VM *vm, void *new_ptr, isize new_size)
{
    if (!new_ptr) {
        debug_fatalf("Out of memory (failed to allocate %ti bytes)", new_size);
        vm_throw_error(vm, LULU_ERROR_MEMORY);
    }
    return new_ptr;
}

void *
mem_alloc(lulu_VM *vm, isize new_size)
{
    void *data    = vm->allocator_data;
    void *new_ptr = vm->allocator(data, new_size, LULU_USER_ALIGNMENT, NULL, 0);
    return wrap_alloc(vm, new_ptr, new_size);
}

void *
mem_resize(lulu_VM *vm, void *old_ptr, isize old_size, isize new_size)
{
    void *data    = vm->allocator_data;
    void *new_ptr = vm->allocator(data, new_size, LULU_USER_ALIGNMENT, old_ptr, old_size);
    return wrap_alloc(vm, new_ptr, new_size);
}

void
mem_free(lulu_VM *vm, void *old_ptr, isize old_size)
{
    void *data = vm->allocator_data;
    vm->allocator(data, 0, LULU_USER_ALIGNMENT, old_ptr, old_size);
}
