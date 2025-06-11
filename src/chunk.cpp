#include <stdio.h>

#include "chunk.h"
#include "debug.h"

void
chunk_init(Chunk &c)
{
    dynamic_init(c.constants);
    dynamic_init(c.code);
    dynamic_init(c.lines);
    c.stack_used = 2; // R(0) and R(1) must always be valid.
}

void
chunk_append(lulu_VM &vm, Chunk &c, Instruction i, int line)
{
    dynamic_push(vm, c.code, i);
    dynamic_push(vm, c.lines, line);
}

u32
chunk_add_constant(lulu_VM &vm, Chunk &c, Value v)
{
    auto &a = c.constants;
    for (size_t i = 0, end = len(a); i < end; i++) {
        if (a[i] == v) {
            return cast(u32, i);
        }
    }
    dynamic_push(vm, a, v);
    return cast(u32, len(a) - 1);
}

void
chunk_destroy(lulu_VM &vm, Chunk &c)
{
    dynamic_delete(vm, c.constants);
    dynamic_delete(vm, c.code);
    dynamic_delete(vm, c.lines);
}
