#include <stdio.h>

#include "chunk.hpp"
#include "debug.hpp"
#include "object.hpp"
#include "vm.hpp"

Chunk *
chunk_new(lulu_VM *vm, String source, Table *indexes)
{
    Chunk *c = object_new<Chunk>(vm, &vm->objects, VALUE_CHUNK);
    dynamic_init(c->constants);
    dynamic_init(c->code);
    dynamic_init(c->line_info);
    c->indexes    = indexes;
    c->source     = source;
    c->stack_used = 2; // R(0) and R(1) must always be valid.
    return c;
}

static void
add_line(lulu_VM *vm, Chunk &c, int pc, int line)
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

int
chunk_append(lulu_VM *vm, Chunk &c, Instruction i, int line)
{
    int pc = cast_int(len(c.code));
    dynamic_push(vm, c.code, i);
    add_line(vm, c, pc, line);
    return pc;
}

int
chunk_get_line(const Chunk &c, int pc)
{
    // Binary search
    size_t left  = 0;
    size_t right = len(c.line_info);
    // left <= right would otherwise pass, yet index 0 is invalid!
    if (right == 0) {
        return NO_LINE;
    }
    while (left <= right) {
        size_t    i    = (left + right) / 2;
        Line_Info info = c.line_info[i];
        if (info.start_pc > pc) {
            // Avoid unsigned overflow
            if (i == 0) {
                break;
            }
            // Current range is greater, ignore this right half.
            right = i - 1;
        } else if (info.end_pc < pc) {
            // Current range is less, ignore this left half.
            left = i + 1;
        } else {
            return info.line;
        }
    }
    return NO_LINE;
}

u32
chunk_add_constant(lulu_VM *vm, Chunk &c, Value v)
{
    bool  ok;
    Value i = table_get(*c.indexes, v, &ok);
    if (ok) {
        return u32(value_to_number(i));
    }

    Number i2 = Number(len(c.constants));
    dynamic_push(vm, c.constants, v);
    table_set(vm, *c.indexes, v, Value(i2));
    return u32(i2);
}
