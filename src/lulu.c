#include "lulu.h"
#include "chunk.h"
#include "debug.h"
#include "value.h"
#include "vm.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief
 *      A simple allocator that wraps the C standard `malloc` family.
 *      
 * @warning 2024-09-04
 *      This will call `abort()` on allocation failure!
 */
static void *heap_allocator_proc(
    void *              allocator_data,
    lulu_Allocator_Mode mode,
    isize               new_size,
    isize               align,
    void *              old_ptr,
    isize               old_size)
{
    void *new_ptr = NULL;
    isize add_len = new_size - old_size;

    unused(allocator_data);
    unused(align);

    switch (mode) {
    case LULU_ALLOCATOR_MODE_ALLOC: // fall through
    case LULU_ALLOCATOR_MODE_RESIZE:
        new_ptr = realloc(old_ptr, new_size);
        if (!new_ptr) {
            fprintf(stderr, "[FATAL]: %s\n", "[Re]allocation failure!");
            fflush(stderr);
            abort();
        }
        // We extended the allocation? Note that immediately loading a possibly
        // invalid pointer is not a safe assumption for 100% of architectures.
        if (add_len > 0) {
            byte *add_ptr = cast(byte *)new_ptr + old_size;
            memset(add_ptr, 0, add_len);
        }
        break;
    case LULU_ALLOCATOR_MODE_FREE:
        free(old_ptr);
        break;
    }
    return new_ptr;
}

int main(int argc, cstring argv[])
{
    lulu_Chunk chunk;
    lulu_Value value;
    lulu_VM    vm;
    
    unused(argc);
    unused(argv);
    
    lulu_Value_set_nil(&value);
    lulu_VM_init(&vm, heap_allocator_proc, NULL);
    lulu_Chunk_init(&chunk, &vm.allocator);
    
    lulu_Value_set_number(&value, 1.2);
    isize index = lulu_Chunk_add_constant(&chunk, &value);
    lulu_Chunk_write(&chunk, OP_CONSTANT, 123);
    lulu_Chunk_write_byte3(&chunk, cast(usize)index, 123);
    
    lulu_Value_set_number(&value, 3.4);
    index = lulu_Chunk_add_constant(&chunk, &value);
    lulu_Chunk_write(&chunk, OP_CONSTANT, 123);
    lulu_Chunk_write_byte3(&chunk, cast(usize)index, 123);

    lulu_Chunk_write(&chunk, OP_ADD, 123);

    lulu_Value_set_number(&value, 5.6);
    index = lulu_Chunk_add_constant(&chunk, &value);
    lulu_Chunk_write(&chunk, OP_CONSTANT, 123);
    lulu_Chunk_write_byte3(&chunk, index, 123);
    
    lulu_Chunk_write(&chunk, OP_DIV, 123);
    lulu_Chunk_write(&chunk, OP_NEGATE, 123);
    lulu_Chunk_write(&chunk, OP_RETURN, 123);
    lulu_Debug_disasssemble_chunk(&chunk, "test chunk");
    
    lulu_VM_interpret(&vm, &chunk);
    lulu_VM_free(&vm);
    lulu_Chunk_free(&chunk);
    return 0;
}
