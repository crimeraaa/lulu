#include "chunk.h"
#include "memory.h"
#include "value.h"

static inline void init_lineRLE(LineRuns *self) {
    self->count    = 0;
    self->cap = 0;
    self->runs     = NULL;
}

void init_chunk(Chunk *self) {
    self->code  = NULL;
    self->count = 0;
    self->cap   = 0;
    self->prevline = -1; // Set to an always invalid line number to start with.
    init_valuearray(&self->constants);
    init_lineRLE(&self->lines);
}

static inline void free_lineRLE(LineRuns *self) {
    deallocate_array(LineRun, self->runs, self->cap);
}

void free_chunk(Chunk *self) {
    deallocate_array(Byte, self->code, self->cap);
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
static void write_lineRLE(LineRuns *self, ptrdiff_t offset, int line) {
    // We should resize this array less often than the bytes one.
    if (self->count + 1 > self->cap) {
        size_t oldcap = self->cap;
        self->cap   = grow_cap(oldcap);
        self->runs  = grow_array(LineRun, self->runs, oldcap, self->cap);
    }
    self->runs[self->count].start = offset;
    self->runs[self->count].end   = offset;
    self->runs[self->count].where = line;
    self->count++;
}

/* Increment the end instruction pointer of the topmost run. */
static inline void increment_lineRLE(LineRuns *self) {
    self->runs[self->count - 1].end++;
}

void write_chunk(Chunk *self, Byte byte, int line) {
    if (self->count + 1 > self->cap) {
        size_t oldcap = self->cap;
        self->cap   = grow_cap(oldcap);
        self->code  = grow_array(Chunk, self->code, oldcap, self->cap);
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

size_t add_constant(Chunk *self, TValue value) {
    write_valuearray(&self->constants, value);
    return self->constants.count - 1;
}

int get_instruction_line(Chunk *self, ptrdiff_t offset) {
    // When iterating, `self->prevline` points to the next index.
    if (offset > 0 && offset <= self->lines.runs[self->prevline - 1].end) {
        return -1;
    }
    return self->lines.runs[self->prevline++].where;
}

/**
 * Only compile these explicitly want debug printout capabilities.
 * Otherwise, don't as they'll take up space in the resulting object file.
 */
#ifdef DEBUG_PRINT_CODE

// static void dump_lineruns(const LineRuns *self) {
//     for (int i = 0; i < self->count; i++) {
//         const LineRun *run = &self->runs[i];
//         printf("line %i: instructions %i-%i\n", run->where, run->start, run->end);
//     }
// }

void disassemble_chunk(Chunk *self, const char *name) {
    // Reset so we start from index 0 into self->lines.runs.
    // Kinda hacky but this will serve as our iterator of sorts.
    self->prevline = 0;
    printf("== %s ==\n", name);
    // We rely on `disassemble_instruction()` for iteration.
    for (ptrdiff_t offset = 0; offset < (ptrdiff_t)self->count;) {
        offset = disassemble_instruction(self, offset);
    }
}

/**
 * Constant instructions take 1 byte for themselves and 1 byte for the operand.
 * The operand is an index into the self's constants pool.
 */
static int constop(const char *name, const Chunk *self, ptrdiff_t offset) {
    // code[offset] is the operation byte itself, code[offset + 1] is the index
    // into the self's constants pool.
    Byte index = self->code[offset + 1];
    printf("%-16s %4i '", name, index);
    print_value(self->constants.values[index]);
    printf("'\n");
    return offset + 2;
}

static inline DWord read_long(const Chunk *self, ptrdiff_t offset) {
    Byte hi  = self->code[offset + 1] << 16; // Unmask upper 8 bits.
    Byte mid = self->code[offset + 2] << 8;  // Unmask center 8 bits.
    Byte lo  = self->code[offset + 3];       // Unmask lower 8 bits.
    return (DWord)(hi | mid | lo);
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
static int constop_L(const char *name, const Chunk *self, ptrdiff_t offset) {
    DWord index = read_long(self, offset);
    printf("%-16s %4u '", name, index);
    print_value(self->constants.values[index]);
    printf("'\n");
    return offset + 4;
}

/* Simple instruction only take 1 byte for themselves. */
static int simpleop(const char *name, ptrdiff_t offset) {
    printf("%s\n", name);
    return offset + 1;
}

/**
 * III:22.4.1   Interpreting local variables
 *
 * `code[offset]` is the opcode, `code[offset + 1]` is the operand.
 */
static int byteop(const char *name, const Chunk *self, ptrdiff_t offset) {
    Byte slot = self->code[offset + 1];
    printf("%-16s %4i\n", name, slot);
    return offset + 2;
}

static inline Word read_short(const Chunk *self, ptrdiff_t offset) {
    Byte hi = self->code[offset + 1] << 8;
    Byte lo = self->code[offset + 2];
    return (Word)(hi | lo);
}

/**
 * III:23.1     If Statements
 *
 * Disassembly support for OP_JUMP* opcodes. Note that we have a `sign` parameter
 * because later on we'll also allow for jumping backwards.
 */
static int jumpop(const char *name, int sign, const Chunk *self, ptrdiff_t offset) {
    Word jump = read_short(self, offset);
    ptrdiff_t target = offset + 3 + sign * jump;
    printf("%-16s    0x%04tx->0x%04tx\n", name, offset, target);
    return offset + 3;
}

int disassemble_instruction(Chunk *self, ptrdiff_t offset) {
    printf("0x%04tx ", offset); // Print number left-padded with 0's
    int line = get_instruction_line(self, offset);
    if (line == -1) {
        printf("   | ");
    } else {
        printf("%4i ", line);
    }
    Byte instruction = self->code[offset];
    switch(instruction) {
    case OP_CONSTANT:  return constop("OP_CONSTANT", self, offset);
    case OP_LCONSTANT: return constop_L("OP_LCONSTANT", self, offset);

    // -*- III:18.4     Two New Types ----------------------------------------*-
    case OP_NIL:   return simpleop("OP_NIL", offset);
    case OP_TRUE:  return simpleop("OP_TRUE", offset);
    case OP_FALSE: return simpleop("OP_FALSE", offset);

    // -*- III:21.1.2   Expression statements --------------------------------*-
    case OP_POP:   return simpleop("OP_POP", offset);
    case OP_POPN:  return byteop("OP_POPN", self, offset);

    // -*- III:22.4.1   Interpreting local variables -------------------------*-
    case OP_GETLOCAL: return byteop("OP_GETLOCAL", self, offset);
    case OP_SETLOCAL: return byteop("OP_SETLOCAL", self, offset);

    // -*- III:21.2     Variable Declarations
    case OP_GETGLOBAL:  return constop("OP_GETGLOBAL", self, offset);
    case OP_LGETGLOBAL: return constop_L("OP_LGETGLOBAL", self, offset);

    // -*- III:21.4     Assignment -------------------------------------------*-
    case OP_SETGLOBAL:  return constop("OP_SETGLOBAL", self, offset);
    case OP_LSETGLOBAL: return constop_L("OP_LSETGLOBAL", self, offset);

    // -*- III:18.4.2   Equality and comparison operators --------------------*-
    case OP_EQ: return simpleop("OP_EQ", offset);
    case OP_GT: return simpleop("OP_GT", offset);
    case OP_LT: return simpleop("OP_LT", offset);

    // -*- III:15.3.1   Binary operators -------------------------------------*-
    case OP_ADD: return simpleop("OP_ADD", offset);
    case OP_SUB: return simpleop("OP_SUB", offset);
    case OP_MUL: return simpleop("OP_MUL", offset);
    case OP_DIV: return simpleop("OP_DIV", offset);
    case OP_POW: return simpleop("OP_POW", offset);
    case OP_MOD: return simpleop("OP_MOD", offset);

    // -*- III:18.4.1   Logical not and falsiness ----------------------------*-
    case OP_NOT:    return simpleop("OP_NOT", offset);

    // -*- III:19.4.1   Concatenation ----------------------------------------*-
    case OP_CONCAT: return simpleop("OP_CONCAT", offset);

    // -*- III:15.3     An Arithmetic Calculator -----------------------------*-
    case OP_UNM:    return simpleop("OP_UNM", offset);

    // -*- III:21.1.1   Print statements
    case OP_PRINT:  return simpleop("OP_PRINT", offset);

    // -*- III:23.1     If Statements ----------------------------------------*-
    case OP_JUMP:          return jumpop("OP_JUMP", 1, self, offset);
    case OP_JUMP_IF_FALSE: return jumpop("OP_JUMP_IF_FALSE", 1, self, offset);
    case OP_RET:           return simpleop("OP_RET", offset);
    default: break;
    }
    printf("Unknown opcode %i.\n", instruction);
    return offset + 1;
}

#endif /* DEBUG_PRINT_CODE */
