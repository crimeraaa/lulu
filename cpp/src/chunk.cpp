#include <stdio.h>

#include "chunk.hpp"
#include "debug.hpp"
#include "object.hpp"
#include "vm.hpp"

Chunk *
chunk_new(lulu_VM *L, OString *source)
{
    Chunk *p = object_new<Chunk>(L, &G(L)->objects, VALUE_CHUNK);
    p->source     = source;
    p->stack_used = 2; // R(0) and R(1) must always be valid.
    return p;
}

void
chunk_delete(lulu_VM *L, Chunk *p)
{
    slice_delete(L, p->locals);
    slice_delete(L, p->upvalues);
    slice_delete(L, p->constants);
    slice_delete(L, p->children);
    slice_delete(L, p->code);
    slice_delete(L, p->lines);
    mem_free(L, p);
}

void
chunk_line_push(lulu_VM *L, Chunk *p, int pc, int line, int *n)
{
    // Have previous lines to go to?
    int i = *n;
    if (i > 0) {
        Line_Info *last = &p->lines[i - 1];
        // Last line is the same as ours, so we can fold this pc range?
        if (last->line == line) {
            // Make sure `pc` is in range and will update things correctly.
            lulu_assertf(last->start_pc <= pc, "start_pc=%i > pc=%i",
                last->start_pc, pc);

            // Use `<=` in case we popped an instruction.
            lulu_assertf(last->end_pc <= pc, "end_pc=%i > pc=%i",
                last->end_pc, pc);

            last->end_pc = pc;
            return;
        }
    }

    Line_Info start{line, pc, pc};
    chunk_push(L, &p->lines, start, n);
}

int
chunk_line_get(const Chunk *p, int pc)
{
    // Binary search
    int left  = 0;
    int right = static_cast<int>(len(p->lines));
    // left <= right would otherwise pass, yet index 0 is invalid!
    if (right == 0) {
        return NO_LINE;
    }
    while (left <= right) {
        int       i    = (left + right) / 2;
        Line_Info info = p->lines[i];
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

const char *
chunk_get_local(const Chunk *p, int local_number, int pc)
{
    int counter = local_number;
    for (Local local : p->locals) {
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
                return local.ident->to_cstring();
            }
        }
    }
    return nullptr;
}
