#include "chunk.h"
#include "memory.h"
#include "value.h"

static inline void init_lineRLE(LineRLE *self) {
    self->count    = 0;
    self->capacity = 0;
    self->runs     = NULL;
}

void init_chunk(Chunk *self) {
    self->code     = NULL;
    self->count    = 0;
    self->capacity = 0;
    self->prevline = -1; // Set to an always invalid line number to start with.
    init_valuearray(&self->constants);
    init_lineRLE(&self->lines);
}

static inline void free_lineRLE(LineRLE *self) {
    deallocate_array(Linerun, self->runs, self->capacity);
}

void free_chunk(Chunk *self) {
    deallocate_array(Byte, self->code, self->capacity);
    free_valuearray(&self->constants);
    free_lineRLE(&self->lines);
    init_chunk(self);
    init_lineRLE(&self->lines);
}

/**
 * BUG:
 * 
 * I was using `Byte` for offset when I should've been using a bigger type.
 * This caused us to overflow whenever a linecount was greater than 256 and thus
 * we didn't write to the proper indexes.
 */
static void write_lineRLE(LineRLE *self, int offset, int line) {
    // We should resize this array less often than the bytes one.
    if (self->count + 1 > self->capacity) {
        int oldcapacity = self->capacity;
        self->capacity  = grow_capacity(oldcapacity);
        self->runs      = grow_array(Linerun, self->runs, oldcapacity, self->capacity);
    }
    self->runs[self->count].start = offset;
    self->runs[self->count].end   = offset;
    self->runs[self->count].where = line;
    self->count++;
}

/* Increment the end instruction pointer of the topmost run. */
static inline void increment_lineRLE(LineRLE *self) {
    self->runs[self->count - 1].end++;
}

void write_chunk(Chunk *self, Byte byte, int line) {
    if (self->count + 1 > self->capacity) {
        int oldcapacity = self->capacity;
        self->capacity  = grow_capacity(oldcapacity);
        self->code      = grow_array(Chunk, self->code, oldcapacity, self->capacity);
    }
    self->code[self->count] = byte;
    // Start a new run for this line number, using byte offset to start the range.
    if (line != self->prevline) {
        write_lineRLE(&self->lines, self->count, line);
        self->prevline = line;
    } else {
        increment_lineRLE(&self->lines);
    }
    self->count++;
}

int add_constant(Chunk *self, TValue value) {
    write_valuearray(&self->constants, value);
    return self->constants.count - 1;
}

int get_instruction_line(Chunk *chunk, int offset) {
    // When iterating, `chunk->prevline` points to the next index.
    if (offset > 0 && offset <= chunk->lines.runs[chunk->prevline - 1].end) {
        return -1;
    }
    return chunk->lines.runs[chunk->prevline++].where;
}

/**
 * Only compile these explicitly want debug printout capabilities.
 * Otherwise, don't as they'll take up space in the resulting object file.
 */
#ifdef DEBUG_PRINT_CODE

// static void dump_lineruns(const LineRLE *self) {
//     for (int i = 0; i < self->count; i++) {
//         const Linerun *run = &self->runs[i];
//         printf("line %i: instructions %i-%i\n", run->where, run->start, run->end);
//     }
// }

void disassemble_chunk(Chunk *self, const char *name) {
    // Reset so we start from index 0 into self->lines.runs.
    // Kinda hacky but this will serve as our iterator of sorts.
    self->prevline = 0;

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
static int constant_instruction(const char *name, const Chunk *chunk, int offset) {
    // code[offset] is the operation byte itself, code[offset + 1] is the index
    // into the chunk's constants pool.
    Byte index = chunk->code[offset + 1];
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
static int constant_instruction_long(const char *name, const Chunk *chunk, int offset) {
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

/**
 * III:22.4.1   Interpreting local variables
 *
 * `code[offset]` is the opcode, `code[offset + 1]` is the operand.
 */
static int byte_instruction(const char *name, const Chunk *chunk, int offset) {
    Byte slot = chunk->code[offset + 1];
    printf("%-16s %4i\n", name, slot);
    return offset + 2;
}

/**
 * III:23.1     If Statements
 *
 * Disassembly support for OP_JUMP* opcodes. Note that we have a `sign` parameter
 * because later on we'll also allow for jumping backwards.
 */
static int jump_instruction(const char *name, int sign, const Chunk *chunk, int offset) {
    Word jump = (chunk->code[offset + 1] << 8) | (chunk->code[offset + 2]);
    int target = offset + 3 + sign * jump;
    printf("%-16s   %04x -> %04x\n", name, offset, target);
    return offset + 3;
}

int disassemble_instruction(Chunk *chunk, int offset) {
    printf("%04x ", offset); // Print number left-padded with 0's

    // Don't print pipe for very first line
    // If lineRLE is still in inclusive range, print pipe
    int line = get_instruction_line(chunk, offset);
    if (line == -1) {
        printf("   | ");
    } else {
        printf("%4i ", line);
    }

    Byte instruction = chunk->code[offset];
    switch(instruction) {
    case OP_CONSTANT:
        return constant_instruction("OP_CONSTANT", chunk, offset);
    case OP_CONSTANT_LONG:
        return constant_instruction_long("OP_CONSTANT_LONG", chunk, offset);
    // -*- III:18.4     Two New Types ----------------------------------------*-
    case OP_NIL:   return simple_instruction("OP_NIL", offset);
    case OP_TRUE:  return simple_instruction("OP_TRUE", offset);
    case OP_FALSE: return simple_instruction("OP_FALSE", offset);

    // -*- III:21.1.2   Expression statements --------------------------------*-
    case OP_POP: return simple_instruction("OP_POP", offset);
    case OP_POPN: return byte_instruction("OP_POPN", chunk, offset);

    // -*- III:22.4.1   Interpreting local variables -------------------------*-
    case OP_GETLOCAL: return byte_instruction("OP_GETLOCAL", chunk, offset);
    case OP_SETLOCAL: return byte_instruction("OP_SETLOCAL", chunk, offset);

    // -*- III:21.2     Variable Declarations
    case OP_GETGLOBAL:
        return constant_instruction("OP_GETGLOBAL", chunk, offset);
    case OP_GETGLOBAL_LONG:
        return constant_instruction_long("OP_GETGLOBAL_LONG", chunk, offset);

    // -*- III:21.4     Assignment -------------------------------------------*-
    case OP_SETGLOBAL:
        return constant_instruction("OP_SETGLOBAL", chunk, offset);
    case OP_SETGLOBAL_LONG:
        return constant_instruction_long("OP_SETGLOBAL_LONG", chunk, offset);

    // -*- III:18.4.2   Equality and comparison operators --------------------*-
    case OP_EQ: return simple_instruction("OP_EQ", offset);
    case OP_GT: return simple_instruction("OP_GT", offset);
    case OP_LT: return simple_instruction("OP_LT", offset);

    // -*- III:15.3.1   Binary operators -------------------------------------*-
    case OP_ADD: return simple_instruction("OP_ADD", offset);
    case OP_SUB: return simple_instruction("OP_SUB", offset);
    case OP_MUL: return simple_instruction("OP_MUL", offset);
    case OP_DIV: return simple_instruction("OP_DIV", offset);
    case OP_POW: return simple_instruction("OP_POW", offset);
    case OP_MOD: return simple_instruction("OP_MOD", offset);

    // -*- III:18.4.1   Logical not and falsiness ----------------------------*-
    case OP_NOT: return simple_instruction("OP_NOT", offset);

    // -*- III:19.4.1   Concatenation ----------------------------------------*-
    case OP_CONCAT: return simple_instruction("OP_CONCAT", offset);

    // -*- III:15.3     An Arithmetic Calculator -----------------------------*-
    case OP_UNM: return simple_instruction("OP_UNM", offset);

    // -*- III:21.1.1   Print statements
    case OP_PRINT: return simple_instruction("OP_PRINT", offset);

    // -*- III:23.1     If Statements ----------------------------------------*-
    case OP_JUMP:
        return jump_instruction("OP_JUMP", 1, chunk, offset);
    case OP_JUMP_IF_FALSE:
        return jump_instruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
    case OP_RET: return simple_instruction("OP_RET", offset);
    default: break;
    }
    printf("Unknown opcode %i.\n", instruction);
    return offset + 1;
}

#endif /* DEBUG_PRINT_CODE */
