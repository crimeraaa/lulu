#include <stdio.h>

#include "chunk.hpp"
#include "debug.hpp"
#include "object.hpp"
#include "vm.hpp"

Chunk *
chunk_new(lulu_VM *vm, LString source, Table *indexes)
{
    Chunk *c = object_new<Chunk>(vm, &vm->objects, VALUE_CHUNK);
    dynamic_init(&c->constants);
    dynamic_init(&c->code);
    dynamic_init(&c->lines);
    dynamic_init(&c->locals);
    c->indexes    = indexes;
    c->source     = source;
    c->stack_used = 2; // R(0) and R(1) must always be valid.
    return c;
}

static void
add_line(lulu_VM *vm, Chunk *c, isize pc, int line)
{
    // Have previous lines to go to?
    isize n = len(c->lines);
    if (n > 0) {
        Line_Info *last = &c->lines[n - 1];
        if (last->line == line) {
            // Make sure `pc` is in range and will update things correctly.
            lulu_assertf(last->start_pc <= pc,
                "start_pc=%" ISIZE_FMTSPEC "> pc=%" ISIZE_FMTSPEC,
                last->start_pc, pc);

            // Use `<=` in case we popped an instruction.
            lulu_assertf(last->end_pc <= pc,
                "end_pc=%" ISIZE_FMTSPEC "> pc=%" ISIZE_FMTSPEC,
                last->end_pc, pc);

            last->end_pc = pc;
            return;
        }
    }

    Line_Info start{line, pc, pc};
    dynamic_push(vm, &c->lines, start);
}

isize
chunk_append(lulu_VM *vm, Chunk *c, Instruction i, int line)
{
    isize pc = len(c->code);
    dynamic_push(vm, &c->code, i);
    add_line(vm, c, pc, line);
    return pc;
}

int
chunk_get_line(const Chunk *c, isize pc)
{
    // Binary search
    isize left  = 0;
    isize right = len(c->lines);
    // left <= right would otherwise pass, yet index 0 is invalid!
    if (right == 0) {
        return NO_LINE;
    }
    while (left <= right) {
        isize     i    = (left + right) / 2;
        Line_Info info = c->lines[i];
        if (info.start_pc > pc) {
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
chunk_add_constant(lulu_VM *vm, Chunk *c, Value v)
{
    Value i;
    if (table_get(c->indexes, v, &i)) {
        return cast(u32)value_to_number(i);
    }

    Number i2 = cast(Number)len(c->constants);
    dynamic_push(vm, &c->constants, v);
    table_set(vm, c->indexes, v, i2);
    return cast(u32)i2;
}

isize
chunk_add_local(lulu_VM *vm, Chunk *c, OString *id)
{
    Local local;
    local.identifier = id;
    local.start_pc   = 0;
    local.end_pc     = 0;

    isize i = len(c->locals);
    dynamic_push(vm, &c->locals, local);
    return i;
}

const char *
chunk_get_local(const Chunk *c, int local_number, isize pc)
{
    int counter = local_number;
    for (Local local : c->locals) {
        // nth local cannot possible be active at this point, and we assume
        // that all succeeding locals won't be either.
        if (local.start_pc > pc) {
            break;
        }

        // Local is valid in this range?
        if (pc <= local.end_pc) {
            counter--;
            // We iterated the correct number of times for this scope?
            if (counter == 0) {
                return ostring_to_cstring(local.identifier);
            }
        }
    }
    return nullptr;
}
