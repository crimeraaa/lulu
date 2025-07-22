#pragma once

#include "chunk.hpp"

LULU_FUNC void
debug_disassemble(const Chunk *c);

LULU_FUNC int
debug_get_pad(const Chunk *c);

LULU_FUNC void
debug_disassemble_at(const Chunk *c, Instruction ip, isize index, int pad);

[[noreturn]]
LULU_FUNC void
debug_type_error(lulu_VM *vm, const char *act, const Value *v);

[[noreturn]]
LULU_FUNC void
debug_arith_error(lulu_VM *vm, const Value *a, const Value *b);

[[noreturn]]
LULU_FUNC void
debug_compare_error(lulu_VM *vm, const Value *a, const Value *b);
