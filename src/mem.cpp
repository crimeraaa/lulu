#include "mem.hpp"
#include "vm.hpp"

void *
mem_rawrealloc(lulu_VM *vm, void *ptr, usize old_size, usize new_size)
{
    void *next = vm->allocator(vm->allocator_data, ptr, old_size, new_size);
    // Allocation request, that wasn't attempting to free, failed?
    if (next == nullptr && new_size != 0) {
        vm_push_string(vm, lstring_from_cstring(LULU_MEMORY_ERROR_STRING));
        vm_throw(vm, LULU_ERROR_MEMORY);
    }
    return next;
}
