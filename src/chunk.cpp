#include <stdio.h>

#include "chunk.hpp"
#include "debug.hpp"

void
chunk_init(Chunk &c, String source)
{
    dynamic_init(c.constants);
    dynamic_init(c.code);
    dynamic_init(c.line_info);
    c.source     = source;
    c.stack_used = 2; // R(0) and R(1) must always be valid.
}

void
add_line(lulu_VM &vm, Chunk &c, int pc, int line)
{
    // Have previous lines to go to?
    if (len(c.line_info) > 0) {
        Line_Info &last = c.line_info[len(c.line_info) - 1];
        if (last.line == line) {
            // Make sure `pc` is in range and will update things correctly.
            lulu_assert(last.start_pc <= pc && last.end_pc < pc);
            last.end_pc = pc;
            return;
        }
    }

    Line_Info start{line, pc, pc};
    dynamic_push(vm, c.line_info, start);
}

void
chunk_append(lulu_VM &vm, Chunk &c, Instruction i, int line)
{
    dynamic_push(vm, c.code, i);
    add_line(vm, c, cast_int(len(c.code) - 1), line);
}

void
chunk_append(lulu_VM &vm, Chunk &c, OpCode op, u8 a, u16 b, u16 c2, int line)
{
    Instruction i = instruction_abc(op, a, b, c2);
    chunk_append(vm, c, i, line);
}

void
chunk_append(lulu_VM &vm, Chunk &c, OpCode op, u8 a, u32 bx, int line)
{
    Instruction i = instruction_abx(op, a, bx);
    chunk_append(vm, c, i, line);
}

int
chunk_get_line(const Chunk &c, int pc)
{
    // Binary search
    size_t stop = len(c.line_info);
    for (size_t i = stop / 2; i < stop;) {
        Line_Info info = c.line_info[i];
        if (info.start_pc > pc) {
            i--; // Current range is greater than us, check left.
        } else if (info.end_pc < pc) {
            i++; // Current range is less than us, check right.
        } else {
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
