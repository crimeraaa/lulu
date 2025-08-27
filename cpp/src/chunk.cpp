#include <stdio.h>

#include "chunk.hpp"
#include "debug.hpp"
#include "object.hpp"
#include "vm.hpp"

Chunk *
chunk_new(lulu_VM *vm, OString *source)
{
    Chunk *p = object_new<Chunk>(vm, &G(vm)->objects, VALUE_CHUNK);
    // Because `c` is heap-allocated, we must explicitly 'construct' the
    // members.
    dynamic_init(&p->locals);
    dynamic_init(&p->upvalues);
    dynamic_init(&p->constants);
    dynamic_init(&p->children);
    p->code = {nullptr, 0};
    dynamic_init(&p->lines);
    p->n_params          = 0;
    p->n_upvalues        = 0;
    p->source            = source;
    p->line_defined      = 0;
    p->last_line_defined = 0;
    p->stack_used        = 2; // R(0) and R(1) must always be valid.
    return p;
}

void
chunk_delete(lulu_VM *vm, Chunk *p)
{
    dynamic_delete(vm, p->locals);
    dynamic_delete(vm, p->upvalues);
    dynamic_delete(vm, p->constants);
    dynamic_delete(vm, p->children);
    slice_delete(vm, p->code);
    dynamic_delete(vm, p->lines);
    mem_free(vm, p);
}

void
chunk_add_line(lulu_VM *vm, Chunk *p, int pc, int line)
{
    // Have previous lines to go to?
    int i = static_cast<int>(len(p->lines));
    if (i > 0) {
        Line_Info *last = &p->lines[i - 1];
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
    dynamic_push(vm, &p->lines, start);
}

int
chunk_add_code(lulu_VM *vm, Chunk *p, Instruction i, int line, int *n)
{
    int pc = chunk_slice_push(vm, &p->code, i, n);
    chunk_add_line(vm, p, pc, line);
    return pc;
}

int
chunk_get_line(const Chunk *p, int pc)
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

u32
chunk_add_constant(lulu_VM *vm, Chunk *p, Value v)
{
    Integer i = len(p->constants);
    dynamic_push(vm, &p->constants, v);
    return static_cast<u32>(i);
}

int
chunk_add_local(lulu_VM *vm, Chunk *p, OString *ident)
{
    Local local{ident, 0, 0};
    int i = static_cast<int>(len(p->locals));
    dynamic_push(vm, &p->locals, local);
    return i;
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
