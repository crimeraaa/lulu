#include "chunk.h"
#include "memory.h"
#include "value.h"

static void init_lineruns(LineRuns *self) {
    self->count = 0;
    self->cap   = 0;
    self->runs  = NULL;
}

static void init_constants(Constants *self) {
    init_table(&self->names);
    init_tarray(&self->values);
}

void init_chunk(Chunk *self) {
    self->code  = NULL;
    self->count = 0;
    self->cap   = 0;
    self->prevline = -1; // Set to an always invalid line number to start with.
    init_constants(&self->constants);
    init_lineruns(&self->lines);
}

static void free_lineruns(LineRuns *self) {
    deallocate_array(LineRun, self->runs, self->cap);
}

static void free_constants(Constants *self) {
    free_table(&self->names);
    free_tarray(&self->values);
}

void free_chunk(Chunk *self) {
    deallocate_array(Byte, self->code, self->cap);
    free_constants(&self->constants);
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
    LineRun *run = &self->runs[self->count++];
    run->start = offset;
    run->end   = offset;
    run->where = line;
}

/* Increment the end instruction pointer of the topmost run. */
static void increment_lineruns(LineRuns *self) {
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

size_t add_constant(Chunk *self, const TValue *value) {
    Constants *constants = &self->constants;
    TValue index;
    // The value-index mapping exists so return the index.
    if (table_get(&constants->names, value, &index)) {
        return index.as.usize;
    }
    // Append the constant value to the values array.
    write_tarray(&constants->values, value);
    
    // Map the given value to its new index as written in the values array.
    index.type      = LUA_TUSIZE;
    index.as.usize  = constants->values.count - 1;
    table_set(&constants->names, value, &index);
    return index.as.usize;
}

int get_linenumber(const Chunk *self, ptrdiff_t offset) {
    const LineRuns *ranges = &self->lines;
    if (ranges->count == 0) {
        return -1;
    }
    size_t left  = 0;                 // Current left half index.
    size_t right = ranges->count - 1; // Current right half index.
    while (left <= right) {
        size_t i = (left + right) / 2; // Index in middle of this half.
        const LineRun *range = &ranges->runs[i];
        if (incrange(offset, range->start, range->end)) {
            return range->where;
        }
        if (offset < range->start) {
            right = i - 1; // Should look to the left            
        } else if (offset > range->end) {
            left  = i + 1; // Should look to the right
        }
    }
    return -1;
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
    for (size_t i = 0; i < self->constants.values.count; i++) {
        printf("constants[%zu]: '", i);
        print_value(&self->constants.values.array[i]);
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
    print_value(&self->constants.values.array[index]);
    printf("'\n");
    return next_instruction(offset, LUA_OPSIZE_BYTE);
}

static DWord read_byte3(const Chunk *self, ptrdiff_t offset) {
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
    print_value(&self->constants.values.array[index]);
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
    int line = get_linenumber(self, offset);
    if (line == self->prevline) {
        printf("   | ");
    } else {
        printf("%4i ", line);
        self->prevline = line;
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
                        
    case OP_FORPREP:    return opsimple("OP_FORPREP", offset);
    case OP_FORCOND:    return opsimple("OP_FORCOND", offset);
    case OP_FORINCR:    return opsimple("OP_FORINCR", offset);

    // -*- III:24.5     Function Calls ---------------------------------------*-
    case OP_ARGS:       return opsimple("OP_ARGS", offset);
    case OP_CALL:       return opbyte("OP_CALL", self, offset);
    case OP_RETURN:     return opsimple("OP_RETURN", offset);
    default:            break;
    }
    printf("Unknown opcode '%i'.\n", instruction);
    return next_instruction(offset, LUA_OPSIZE_NONE);
}

#endif /* DEBUG_PRINT_CODE */
