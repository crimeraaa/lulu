#include <stdio.h>

#include "chunk.h"
#include "debug.h"

void
chunk_init(Chunk &c)
{
    dynamic_init(c.constants);
    dynamic_init(c.code);
    dynamic_init(c.line_info);
    c.stack_used = 2; // R(0) and R(1) must always be valid.
}

void
add_line(lulu_VM &vm, Chunk &c, int pc, int line)
{
    for (auto &info : c.line_info) {
        // Since we're iterating forwards, we can assume that once `start_pc`
        // is greater than ours that we won't find `line` to begin with.
        if (info.start_pc > pc) {
            break;
        }
        
        // We found the info that's within range of us, so update it.
        // We return because there's nothing more to do.
        if (info.line == line && info.end_pc < pc) {
            info.end_pc = pc;
            return;
        }
    }
    
    Line_Info start;
    start.line     = line;
    start.start_pc = pc;
    start.end_pc   = NO_LINE;
    dynamic_push(vm, c.line_info, start);
}

void
chunk_append(lulu_VM &vm, Chunk &c, Instruction i, int line)
{
    dynamic_push(vm, c.code, i);
    add_line(vm, c, cast_int(len(c.code) - 1), line);
}

int
chunk_get_line(const Chunk &c, int pc)
{
    for (const auto &info : c.line_info) {
        // Same assumptions as in `add_line()`.
        if (info.start_pc > pc) {
            break;
        }

        // Implicit negation of above: `start_pc <= pc`
        if (pc <= info.end_pc) {
            return info.line;            
        }
    }
    return NO_LINE;
}

u32
chunk_add_constant(lulu_VM &vm, Chunk &c, Value v)
{
    auto &a = c.constants;
    for (size_t i = 0, end = len(a); i < end; i++) {
        if (value_eq(v, a[i])) {
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
    dynamic_delete(vm, c.line_info);
}
