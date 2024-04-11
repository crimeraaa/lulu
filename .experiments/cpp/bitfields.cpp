/* NOTE: Only using C++ for consistency, please mainly write C here! */
#include <cstdio>
#include <climits>
#include <string>

#define BITS_PER_BYTE   CHAR_BIT
#define bitsize(T)      (sizeof(T) * BITS_PER_BYTE)

constexpr int SIZE_OP = 6,
              SIZE_A  = 8,
              SIZE_B  = 9,
              SIZE_C  = 9,
              SIZE_Bx = (SIZE_B + SIZE_C);

constexpr int MAXARG_A  = ((1 << SIZE_A) - 1),
              MAXARG_B  = ((1 << SIZE_B) - 1),
              MAXARG_C  = MAXARG_B,
              MAXARG_Bx = ((1 << SIZE_Bx) - 1);

enum OpCode : unsigned int {
    OP_CONSTANT,
    OP_RETURN,
};

struct Instruction {
    OpCode op : SIZE_OP;
    unsigned int a  : SIZE_A;
    unsigned int b  : SIZE_B;
    unsigned int c  : SIZE_C;
};

#define create_ABC(op, a, b, c) \
    {op, a, b, c}

/* Encode `bx` into arguments B (the MSB) and C (the LSB). */
#define create_ABx(op, a, bx) \
    {op, a, ((bx) >> SIZE_B) & MAXARG_Bx, (bx) & MAXARG_Bx}

// Determine if we need to add a `'_'` character, for pretty printing.
bool isbytegroup(int index, int bitsize) {
    switch (bitsize) {
    case SIZE_OP:
    case SIZE_A:
        return (index > 0 and (index % BITS_PER_BYTE) == 0);
    case SIZE_B: // case SIZE_C:
        return index == 1;
    case SIZE_Bx:
        return index == 2 or ((index - 2) % BITS_PER_BYTE) == 0;
    default:
        return false;
    }
}

const char* opname(OpCode opcode) {
    switch (opcode) {
    case OP_CONSTANT: return "OP_CONSTANT";
    case OP_RETURN:   return "OP_RETURN";
    }
}

std::string tobinary(unsigned int value, int bits) {
    std::string buffer{"0b"};
    unsigned int mask = 1U << (bits - 1); // Use to extract current MSB.

    buffer.reserve(bits); // We already know the maximum possible bit length.
    for (int i = 0; i < bits; i++) {
        if (isbytegroup(i, bits)) {
            buffer.push_back('_');
        }
        // Check value of bit and convert to character representation.
        buffer.push_back((value & mask) ? '1' : '0');
        value <<= 1; // Remove current MSB, update MSB to its right bit
    }
    return buffer;
}

void print_iABC(const Instruction& self) {
    std::printf(
        "instruction := {op := %s,\n"
        "                 a := %s,\n"
        "                 b := %s,\n"
        "                 c := %s}\n",
        opname(self.op), 
        tobinary(self.a, SIZE_A).c_str(),
        tobinary(self.b, SIZE_B).c_str(), 
        tobinary(self.c, SIZE_C).c_str());
}

void print_iABx(const Instruction& self) {
    // Combine B and C into an 18-bit unsigned integer, using B as MSB.
    unsigned int bx = (self.b << SIZE_B) | self.c;
    std::printf(
        "instruction := {op := %s,\n"
        "                 a := %s,\n"
        "                bx := %s}\n",
        opname(self.op),
        tobinary(self.a, SIZE_A).c_str(),
        tobinary(bx, SIZE_Bx).c_str()
    );
}

int main() {
    Instruction i  = create_ABC(OP_CONSTANT, 13, 13, 7);
    Instruction ii = create_ABx(OP_RETURN, 9, 13);
    std::printf("sizeof(Instruction) := %zu\n", sizeof(i));
    print_iABC(i);
    print_iABx(ii);
    return 0;
}
