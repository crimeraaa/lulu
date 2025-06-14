#include "mem.hpp"
#include "vm.hpp"

void *
mem_rawrealloc(lulu_VM &vm, void *ptr, size_t old_size, size_t new_size)
{
    void *next = vm.allocator(vm.allocator_data, ptr, old_size, new_size);
    // Allocation request, that wasn't attempting to free, failed?
    if (new_size != 0 && next == nullptr) {
        vm_throw(vm, LULU_ERROR_MEMORY);
    }
    return next;
}

size_t
mem_next_size(size_t n)
{
    size_t next = 8;
    while (next <= n) {
        next *= 2;
    }
    return next;
}
