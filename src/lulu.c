#include "lulu.h"
#include "chunk.h"
#include "debug.h"
#include "value.h"

#include <stdio.h>

int main(int argc, cstring argv[])
{
    lulu_Chunk chunk;
    lulu_Value value;
    
    unused(argc);
    unused(argv);
    
    lulu_Value_set_nil(&value);
    lulu_Chunk_init(&chunk, &lulu_heap_allocator);
    
    lulu_Value_set_number(&value, 1.2);
    isize index = lulu_Chunk_add_constant(&chunk, &value);
    lulu_Chunk_write(&chunk, OP_CONSTANT, 123);
    lulu_Chunk_write_byte3(&chunk, cast(usize)index, 123);
    
    lulu_Chunk_write(&chunk, OP_RETURN, 123);
    lulu_Debug_disasssemble_chunk(&chunk, "test chunk");
    lulu_Chunk_free(&chunk);    
    return 0;
}
