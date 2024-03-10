#include "chunk.h"
#include "memory.h"
#include "value.h"

static inline void init_lineruns(LineRuns *self) {
    self->count = 0;
    self->cap   = 0;
    self->runs  = NULL;
}

void init_chunk(Chunk *self) {
    self->code  = NULL;
    self->count = 0;
    self->cap   = 0;
    self->prevline = -1; // Set to an always invalid line number to start with.
    init_valuearray(&self->constants);
    init_lineruns(&self->lines);
}

static inline void free_lineruns(LineRuns *self) {
    deallocate_array(LineRun, self->runs, self->cap);
}

void free_chunk(Chunk *self) {
    deallocate_array(Byte, self->code, self->cap);
    free_valuearray(&self->constants);
    free_lineruns(&self->lines);
    init_chunk(self);
    init_lineruns(&self->lines);
}

/**
 * BUG:
 * 
 * I was using `Byte` for offset when I should've been using a bigger type.
 * This caused us to overflow whenever a linecount was greater than 256 and thus
 * we didn't write to the proper indexes.
 */
static void write_lineruns(LineRuns *self, int offset, int line) {
    // We should resize this array less often than the bytes one.
    if (self->count + 1 > self->cap) {
        size_t oldcap = self->cap;
        self->cap     = grow_cap(oldcap);
        self->runs    = grow_array(LineRun, self->runs, oldcap, self->cap);
    }
    self->runs[self->count].start = offset;
    self->runs[self->count].end   = offset;
    self->runs[self->count].where = line;
    self->count++;
}

/* Increment the end instruction pointer of the topmost run. */
static inline void increment_lineruns(LineRuns *self) {
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
        // size_t being downcast to int, might cause signed overflow
        write_lineruns(&self->lines, self->count, line);
        self->prevline = line;
    } else {
        increment_lineruns(&self->lines);
    }
    self->count++;
}

size_t add_constant(Chunk *self, TValue value) {
    // Linear search is inefficient but I really do not care
    for (size_t i = 0; i < self->constants.count; i++) {
        if (values_equal(&self->constants.values[i], &value)) {
            return i;
        }
    }
    write_valuearray(&self->constants, value);
    return self->constants.count - 1;
}

int current_line(const Chunk *self) {
    return self->lines.runs[self->prevline - 1].where;
}

int next_line(Chunk *self, ptrdiff_t offset) {
    // When iterating, `self->prevline` points to the next index.
    if (offset > 0 && offset <= self->lines.runs[self->prevline - 1].end) {
        return -1;
    }
    self->prevline++;
    return current_line(self);
}

/**
 * Only compile these explicitly want debug printout capabilities.
 * Otherwise, don't as they'll take up space in the resulting object file.
 */
#ifdef DEBUG_PRINT_CODE
#define next_instruction(offset, opsize)    ((offset) + 1 + (opsize))

void disassemble_chunk(Chunk *self, const char *name) {
    // Reset so we start from index 0 into self->lines.runs.
    // Kinda hacky but this will serve as our iterator of sorts.
    self->prevline = 0;
    printf("=== %s ===\n", name);
    for (size_t i = 0; i < self->constants.count; i++) {
        printf("constants[%zu]: '", i);
        print_value(&self->constants.values[i]);
        printf("'\n");
    }
    printf("\n");
    // We rely on `disassemble_instruction()` for iteration.
    for (ptrdiff_t offset = 0; offset < (ptrdiff_t)self->count;) {
        offset = disassemble_instruction(self, offset);
    }
    printf("\n");
}

/**
 * Constant instructions take 1 byte for themselves and 1 byte for the operand.
 * The operand is an index into the self's constants pool.
 */
static int opconst(const char *name, const Chunk *self, ptrdiff_t offset) {
    // code[offset] is the operation byte itself, code[offset + 1] is the index
    // into the self's constants pool.
    Byte index = self->code[offset + 1];
    printf("%-16s %4i '", name, index);
    print_value(&self->constants.values[index]);
    printf("'\n");
    return next_instruction(offset, LUA_OPSIZE_BYTE);
}

static inline DWord read_byte3(const Chunk *self, ptrdiff_t offset) {
    DWord msb = byteunmask(self->code[offset + 1], 2); // Unmask upper 8 bits.
    DWord mid = byteunmask(self->code[offset + 2], 1); // Unmask middle 8 bits.
    DWord lsb = byteunmask(self->code[offset + 3], 0); // Unmask lower 8 bits.
    return msb | mid | lsb;
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
static int oplconst(const char *name, const Chunk *self, ptrdiff_t offset) {
    DWord index = read_byte3(self, offset);
    printf("%-16s %4u '", name, index);
    print_value(&self->constants.values[index]);
    printf("'\n");
    return next_instruction(offset, LUA_OPSIZE_BYTE3);
}

/* Simple instruction only take 1 byte for themselves. */
static int opsimple(const char *name, ptrdiff_t offset) {
    printf("%s\n", name);
    return next_instruction(offset, LUA_OPSIZE_NONE);
}

/**
 * III:22.4.1   Interpreting local variables
 *
 * `code[offset]` is the opcode, `code[offset + 1]` is the operand.
 */
static int opbyte(const char *name, const Chunk *self, ptrdiff_t offset) {
    Byte slot = self->code[offset + 1];
    printf("%-16s %4i\n", name, slot);
    return next_instruction(offset, LUA_OPSIZE_BYTE);
}

static Word read_byte2(const Chunk *self, ptrdiff_t offset) {
    Word msb = byteunmask(self->code[offset + 1], 1);
    Word lsb = byteunmask(self->code[offset + 2], 0);
    return msb | lsb;
}

/**
 * III:23.1     If Statements
 *
 * Disassembly support for OP_JMP* opcodes. Note that we have a `sign` parameter
 * because later on we'll also allow for jumping backwards.
 */
static int opjump(const char *name, int sign, const Chunk *self, ptrdiff_t offset) {
    Word jump = read_byte2(self, offset);
    int next = next_instruction(offset, LUA_OPSIZE_BYTE2);
    ptrdiff_t target = next + (sign * jump);
    printf("%-16s    0x%04tx->0x%04tx\n", name, offset, target);
    return next;
}

int disassemble_instruction(Chunk *self, ptrdiff_t offset) {
    printf("0x%04tx ", offset); // Print number left-padded with 0's
    int line = next_line(self, offset);
    if (line == -1) {
        printf("   | ");
    } else {
        printf("%4i ", line);
    }
    Byte instruction = self->code[offset];
    switch(instruction) {
    case OP_CONSTANT:   return opconst("OP_CONSTANT",   self, offset);
    case OP_LCONSTANT:  return oplconst("OP_LCONSTANT", self, offset);

    // -*- III:18.4     Two New Types ----------------------------------------*-
    case OP_NIL:        return opsimple("OP_NIL",   offset);
    case OP_TRUE:       return opsimple("OP_TRUE",  offset);
    case OP_FALSE:      return opsimple("OP_FALSE", offset);

    // -*- III:21.1.2   Expression statements --------------------------------*-
    case OP_POP:        return opsimple("OP_POP", offset);
    case OP_NPOP:       return opbyte("OP_NPOP", self, offset);

    // -*- III:22.4.1   Interpreting local variables -------------------------*-
    case OP_GETLOCAL:   return opbyte("OP_GETLOCAL", self, offset);
    case OP_SETLOCAL:   return opbyte("OP_SETLOCAL", self, offset);

    // -*- III:21.2     Variable Declarations
    case OP_GETGLOBAL:  return opconst("OP_GETGLOBAL",   self, offset);
    case OP_LGETGLOBAL: return oplconst("OP_LGETGLOBAL", self, offset);

    // -*- III:21.4     Assignment -------------------------------------------*-
    case OP_SETGLOBAL:  return opconst("OP_SETGLOBAL",   self, offset);
    case OP_LSETGLOBAL: return oplconst("OP_LSETGLOBAL", self, offset);

    // -*- III:18.4.2   Equality and comparison operators --------------------*-
    case OP_EQ:         return opsimple("OP_EQ", offset);
    case OP_GT:         return opsimple("OP_GT", offset);
    case OP_LT:         return opsimple("OP_LT", offset);

    // -*- III:15.3.1   Binary operators -------------------------------------*-
    case OP_ADD:        return opsimple("OP_ADD", offset);
    case OP_SUB:        return opsimple("OP_SUB", offset);
    case OP_MUL:        return opsimple("OP_MUL", offset);
    case OP_DIV:        return opsimple("OP_DIV", offset);
    case OP_POW:        return opsimple("OP_POW", offset);
    case OP_MOD:        return opsimple("OP_MOD", offset);

    // -*- III:18.4.1   Logical not and falsiness ----------------------------*-
    case OP_NOT:        return opsimple("OP_NOT", offset);

    // -*- III:19.4.1   Concatenation ----------------------------------------*-
    case OP_CONCAT:     return opsimple("OP_CONCAT", offset);

    // -*- III:15.3     An Arithmetic Calculator -----------------------------*-
    case OP_UNM:        return opsimple("OP_UNM", offset);

    // -*- III:23.1     If Statements ----------------------------------------*-
    case OP_JMP:        return opjump("OP_JMP",  1, self, offset);
    case OP_FJMP:       return opjump("OP_FJMP", 1, self, offset);

    // -*- III:23.3     While Statements -------------------------------------*-
    case OP_LOOP:       return opjump("OP_LOOP", -1, self, offset);

    // -*- III:24.5     Function Calls ---------------------------------------*-
    case OP_CALL:       return opbyte("OP_CALL", self, offset);
    case OP_RETURN:     return opsimple("OP_RETURN", offset);
    default:            break;
    }
    printf("Unknown opcode '%i'.\n", instruction);
    return next_instruction(offset, LUA_OPSIZE_NONE);
}

#endif /* DEBUG_PRINT_CODE */
