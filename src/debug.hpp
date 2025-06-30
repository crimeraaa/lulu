#pragma once

#include "chunk.hpp"

LULU_FUNC void
debug_disassemble(const Chunk *c);

LULU_FUNC int
debug_get_pad(const Chunk *c);

LULU_FUNC void
debug_disassemble_at(const Chunk *c, int pc, int pad);
