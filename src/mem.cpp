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

isize
mem_next_pow2(isize n)
{
    isize next = 8;
    while (next <= n) {
        // x << 1 <=> x * 2 if x is a power of 2
        next <<= 1;
    }
    return next;
}

isize
mem_next_fib(isize n)
{
    // Use 8 at the first size for optimization.
    isize next = 8;
    while (next <= n) {
        // (x*3) >> 1 <=> (x*3) / 2 <=> x*1.5
        // 1.5 is the closest approximation of the Fibonacci factor of 1.618...
        next = (next * 3) >> 1;
    }
    return next;
}
