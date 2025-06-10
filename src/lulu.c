#include <stdlib.h>

#include "lulu.h"
#include "chunk.h"
#include "vm.h"

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
    vm_init(&vm, &c_allocator, nullptr);
    chunk_init(&c);

    uint32_t k1 = chunk_add_constant(&vm, &c, 9);
    uint32_t k2 = chunk_add_constant(&vm, &c, 10);
    chunk_append(&vm, &c, instruction_abc(OP_ADD, 0, rk_make(k1), rk_make(k2)));
    chunk_append(&vm, &c, instruction_abc(OP_RETURN, 0, 0, 0));
    chunk_dump_all(&c);

    chunk_destroy(&vm, &c);
    return 0;
}
