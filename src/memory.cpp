#include "memory.hpp"
#include "vm.hpp"

void *
raw_dynamic_grow(lulu_VM &vm, Raw_Dynamic &self, size_t type_size)
{
    size_t old_cap = self.cap;
    size_t new_cap = (old_cap == 0) ? 8 : 1;
    while (new_cap <= old_cap) {
        new_cap *= 2;
    }
    self.cap = new_cap;
    return mem_realloc(vm, self.data, old_cap * type_size, new_cap * type_size);
}

void *
mem_realloc(lulu_VM &vm, void *ptr, size_t old_size, size_t new_size)
{
    void *next = vm.allocator(vm.allocator_data, ptr, old_size, new_size);
    // Only free-like requests should return a nullptr.
    if (next == nullptr && old_size > 0 && new_size == 0) {
        // what do?
    }
    return next;
}
