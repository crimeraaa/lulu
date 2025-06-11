#include "mem.h"
#include "vm.h"

void *
mem_rawrealloc(lulu_VM &vm, void *ptr, size_t old_size, size_t new_size)
{
    return vm.allocator(vm.allocator_data, ptr, old_size, new_size);
}

size_t
mem_next_size(size_t n)
{
    size_t next = (n == 0) ? 8 : n;
    while (next <= n) {
        next *= 2;
    }
    return next;
}
