#pragma once

#include "chunk.h"

// Disable name mangling
extern "C" {

void
debug_disassemble(const Chunk &c);

int
debug_get_pad(const Chunk &c);

void
debug_disassemble_at(const Chunk &c, Instruction ip, int pc, int pad);

}; // extern "C"
