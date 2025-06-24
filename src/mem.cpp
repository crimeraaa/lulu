#include "mem.hpp"
#include "vm.hpp"

void *
mem_rawrealloc(lulu_VM &vm, void *ptr, size_t old_size, size_t new_size)
{
    void *next = vm.allocator(vm.allocator_data, ptr, old_size, new_size);
    // Allocation request, that wasn't attempting to free, failed?
    if (next == nullptr && new_size != 0) {
        vm_throw(vm, LULU_ERROR_MEMORY);
    }
    return next;
}

size_t
mem_next_size(size_t n)
{
    size_t next = 8;
    while (next <= n) {
        // x << 1 <=> x * 2 if x is a power of 2
        next <<= 1;
    }
    return next;
}
