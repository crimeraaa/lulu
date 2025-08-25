#pragma once

#include "chunk.hpp"

void
debug_disassemble(const Chunk *p);

int
debug_get_pad(const Chunk *p);

void
debug_disassemble_at(const Chunk *p, Instruction ip, int index, int pad);

[[noreturn]] void
debug_type_error(lulu_VM *vm, const char *act, const Value *v);

[[noreturn]] void
debug_arith_error(lulu_VM *vm, const Value *a, const Value *b);

[[noreturn]] void
debug_compare_error(lulu_VM *vm, const Value *a, const Value *b);
