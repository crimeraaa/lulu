#include <stdlib.h>

#include "lulu.hpp"
#include "vm.hpp"

static void *
c_allocator(void *user_data, void *ptr, size_t old_size, size_t new_size)
{
    (void)user_data;
    (void)old_size;
    if (new_size == 0) {
        free(ptr);
        return nullptr;
    }
    return realloc(ptr, new_size);
}

int
main()
{
    lulu_VM vm;
    Chunk c;

    vm.allocator      = c_allocator;
    vm.allocator_data = nullptr;
    
    u32 index = chunk_add_constant(vm, c, 9);
    chunk_append(vm, c, Instruction(OP_LOAD_CONSTANT, /* reg */ 0, index));
    chunk_append(vm, c, Instruction(OP_RETURN));
    chunk_list(c);
    vm_execute(vm, c);
    chunk_destroy(vm, c);
    return 0;
}
