#include "chunk.h"
#include "memory.h"
#include "value.h"

void init_linerun(LuaLineRun *self, uint8_t byte, int line);
void init_lineRLE(LuaLineRLE *self);
void deinit_lineRLE(LuaLineRLE *self);
void write_lineRLE(LuaLineRLE *self, uint8_t byte, int line);

void init_chunk(LuaChunk *self) {
    self->code = NULL;
    self->count = 0;
    self->capacity = 0;
    self->prevline = -1; // Set to an always invalid line number to start with.
    init_valuearray(&self->constants);
    init_lineRLE(&self->lines);
}

void deinit_chunk(LuaChunk *self) {
    deallocate_array(uint8_t, self->code, self->capacity);
    deinit_valuearray(&self->constants);
    deinit_lineRLE(&self->lines);
    init_chunk(self);
    init_lineRLE(&self->lines);
}

void init_lineRLE(LuaLineRLE *self) {
    self->count = 0;
    self->capacity = 0;
    self->runs = NULL;
}

void deinit_lineRLE(LuaLineRLE *self) {
    deallocate_array(LuaLineRun, self->runs, self->capacity);
}

/* Increment the end instruction pointer of the topmost run. */
static inline void increment_lineRLE(LuaLineRLE *self) {
    self->runs[self->count - 1].end++;
}

void write_chunk(LuaChunk *self, uint8_t byte, int line) {
    if (self->count + 1 > self->capacity) {
        int oldcapacity = self->capacity;
        self->capacity = grow_capacity(oldcapacity);
        self->code = grow_array(LuaChunk, self->code, oldcapacity, self->capacity);
    }
    self->code[self->count] = byte;
    self->count++;
    // Start a new run for this line number.
    if (line != self->prevline) {
        write_lineRLE(&self->lines, byte, line);
        self->prevline = line;
    } else {
        increment_lineRLE(&self->lines);
    }
}

void write_lineRLE(LuaLineRLE *self, uint8_t byte, int line) {
    // We should resize this array less often than the bytes one.
    if (self->count + 1 > self->capacity) {
        int oldcapacity = self->capacity;
        self->capacity = grow_capacity(oldcapacity);
        self->runs = grow_array(LuaLineRun, self->runs, oldcapacity, self->capacity);
    }
    init_linerun(&self->runs[self->count], byte, line);
    self->count++;
}

void init_linerun(LuaLineRun *self, uint8_t byte, int line) {
    self->start = byte;
    self->end   = byte;
    self->where = line;
}

int add_constant(LuaChunk *self, LuaValue value) {
    write_valuearray(&self->constants, value);
    return self->constants.count - 1;
}

void write_constant(LuaChunk *self, LuaValue value, int line) {
    int index = add_constant(self, value);
    if (self->constants.count <= UINT8_MAX) {
        write_chunk(self, OP_CONSTANT, line);
        write_chunk(self, index, line);
    } else {
        write_chunk(self, OP_CONSTANT_LONG, line);
        write_chunk(self, (index >> 16) & 0xFF, line); // mask upper 8 bits
        write_chunk(self, (index >> 8) & 0xFF, line);  // mask center 8 bits
        write_chunk(self, (index & 0xFF), line);       // mask lower 8 bits
    }
}

void disassemble_chunk(LuaChunk *self, const char *name) {
    // Reset so we start from index 0 into self->lines.runs.
    // Kinda hacky but this will serve as our iterator of sorts.
    self->prevline = -1;

    printf("== %s ==\n", name);
    // We rely on `disassemble_instruction()` for iteration.
    for (int offset = 0; offset < self->count;) {
        offset = disassemble_instruction(self, offset);
    }
}

/**
 * Constant instructions take 1 byte for themselves and 1 byte for the operand. 
 * The operand is an index into the chunk's constants pool.
 */
static int constant_instruction(const char *name, LuaChunk *chunk, int offset) {
    // code[offset] is the operation byte itself, code[offset + 1] is the index
    // into the chunk's constants pool.
    uint8_t index = chunk->code[offset + 1];
    printf("%-16s %4i '", name, index);
    print_value(chunk->constants.values[index]);
    printf("'\n");
    return offset + 2;
}

/**
 * Challenge III:14.1: Extended Width Instructions
 * 
 * When loading the 256th-(2^24)th constant, we need to use a 3 byte operand.
 *
 * 1. code[offset + 1]: Upper 8 bits of the operand.
 * 2. code[offset + 2]: Middle 8 bits of the operand.
 * 3. code[offset + 3]: Lower 8 bits of the operand.
 * 
 * We have to use some clever bit twiddling to construct a 32-bit integer.
 * 
 * In total, this entire operation takes up 4 bytes.
 */
static int constant_long_instruction(const char *name, LuaChunk *chunk, int offset) {
    int index = chunk->code[offset + 1] << 16; // Unmask upper 8 bits
    index |= chunk->code[offset + 2] << 8;     // Unmask center 8 bits
    index |= chunk->code[offset + 3];          // Unmask lower 8 bits
    printf("%-16s %4i '", name, index);        // We now have a 24-bit integer!
    print_value(chunk->constants.values[index]);
    printf("'\n");
    return offset + 4;
}

/* Simple instruction only take 1 byte for themselves. */
static int simple_instruction(const char *name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

int disassemble_instruction(LuaChunk *self, int offset) {
    printf("%04i ", offset); // Print number left-padded with 0's

    // Don't print pipe for very first line
    // If lineRLE is still in inclusive range, print pipe
    if (offset > 0 && offset <= self->lines.runs[self->prevline].end) {
        printf("   | ");
    } else {
        self->prevline++;
        printf("%4i ", self->lines.runs[self->prevline].where);
    }

    uint8_t instruction = self->code[offset];
    switch(instruction) {
    case OP_CONSTANT: return constant_instruction("OP_CONSTANT", self, offset);
    case OP_CONSTANT_LONG: return constant_long_instruction("OP_CONSTANT_LONG", self, offset);
    case OP_RETURN:   return simple_instruction("OP_RETURN", offset);
    default: break;
    }
    printf("Unknown opcode %i.\n", instruction);
    return offset + 1;
}
