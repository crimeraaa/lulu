#include <stdlib.h>

#include "lulu.h"
#include "chunk.h"
#include "vm.h"
#include "debug.h"

static void *
c_allocator(void *user_data, void *ptr, size_t old_size, size_t new_size)
{
    unused(user_data);
    unused(old_size);
    if (new_size == 0) {
        free(ptr);
        return nullptr;
    }
    return realloc(ptr, new_size);
}

int main(void)
{
    lulu_VM vm;
    Chunk c;
    vm_init(vm, &c_allocator, nullptr);
    chunk_init(c);

    u32 k1 = chunk_add_constant(vm, c, 9);
    u32 k2 = chunk_add_constant(vm, c, 10);
    int stack_used = 9;
    int line = 9001;
    chunk_append(vm, c, instruction_abc(OP_ADD, u8(stack_used++), reg_to_rk(k1), reg_to_rk(k2)), line);
    chunk_append(vm, c, instruction_abc(OP_RETURN, 0, 0, 0), line);

    // if (stack_used > c.stack_used) {
    //     c.stack_used = stack_used;
    // }

    debug_disassemble(c);
    vm_execute(vm, c);
    chunk_destroy(vm, c);
    return 0;
}
