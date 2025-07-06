#pragma once

#include "chunk.hpp"

LULU_FUNC void
debug_disassemble(const Chunk *c);

LULU_FUNC int
debug_get_pad(const Chunk *c);

LULU_FUNC void
debug_disassemble_at(const Chunk *c, isize pc, int pad);


/**
 * @param local_number
 *      The 1-based index of the local we want to get.
 *
 * @param pc
 *      The index of the instruction where `local_number` is valid.
 */
LULU_FUNC const char *
debug_get_local(const Chunk *c, int local_number, isize pc);
