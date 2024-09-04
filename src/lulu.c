#include "lulu.h"
#include "chunk.h"
#include "debug.h"
#include "value.h"

#include <stdio.h>

int main(int argc, cstring argv[])
{
    const lulu_Allocator *allocator = &lulu_heap_allocator;
    lulu_Chunk chunk;
    lulu_Value value;
    
    unused(argc);
    unused(argv);
    
    lulu_Value_set_nil(&value);
    lulu_Chunk_init(&chunk);
    
    // TODO(2024-09-04): use multi-byte arguments
    lulu_Value_set_number(&value, 1.2);
    isize index = lulu_Chunk_add_constant(&chunk, &value, allocator);
    lulu_Chunk_write(&chunk, OP_CONSTANT, 123, allocator);
    lulu_Chunk_write(&chunk, cast(byte)index, 123, allocator);
    

    lulu_Chunk_write(&chunk, OP_RETURN, 123, allocator);
    lulu_Debug_disasssemble_chunk(&chunk, "test chunk");
    lulu_Chunk_free(&chunk, allocator);    
    return 0;
}
