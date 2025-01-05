#+private
package lulu

SIZE_B      :: 9
SIZE_C      :: 9
SIZE_A      :: 8
SIZE_BC     :: SIZE_B + SIZE_C      // 9 + 9 = 18
SIZE_OP     :: 6

MAX_B       :: 1<<SIZE_B  - 1       // 1<<9 - 1  = 0b1_1111_1111
MAX_C       :: 1<<SIZE_C  - 1       // 1<<9 - 1  = 0b1_1111_1111
MAX_uBC     :: 1<<SIZE_BC - 1       // 1<<18 - 1 = 0b11_1111_1111_1111_1111
MAX_A       :: 1<<SIZE_A  - 1       // 1<<8 - 1  = 0b1111_1111
MAX_sBC     :: 1<<(SIZE_BC - 1) - 1 // 1<<(18 - 1) -1 = 0b1_1111_1111_1111_111
MAX_OP      :: 1<<SIZE_OP - 1       // (1 << 6)  - 1 = 0b0011_1111

// Starting bit indexes.
OFFSET_B    :: OFFSET_C  + SIZE_C   // 14 + 9 = 23
OFFSET_C    :: OFFSET_A  + SIZE_A   // 6  + 8 = 14
OFFSET_BC   :: OFFSET_C             // = 14
OFFSET_A    :: OFFSET_OP + SIZE_OP  // 0 + 6  = 6
OFFSET_OP   :: 0

Instruction :: bit_field u32 {
    b:  u16    | SIZE_B, // 9th bit is sign or flag
    c:  u16    | SIZE_C, // 9th bit is sign, or flag, or combined into B
    a:  u16    | SIZE_A, // Always unsigned
    op: OpCode | SIZE_OP  `fmt:"s"`,
}

OpCode :: enum u8 {
/* =============================================================================
Note on shorthand:
(*) Reg:
    - Register
    - requires absolute index into stack
    - Reg[A] => often the destination operand
(*) Kst:
    - Constants Table
    - From current Chunk
    - Requires absolute index
(*) RK:
    - Register or Constants Table
    - If argument B or C is >= 256, meaning bit 9 is toggled, then it is an index
      into Kst. Otherwise, it is a Reg.
============================================================================= */
//          Args        | Description
Constant,   // A, uBC   |   Reg[A] := Kst[uBC]
Add,        // A, B, C  |   Reg[A] := RK[B] + RK[C]
Sub,        // A, B, C  |   Reg[A] := RK[B] - RK[C]
Mul,        // A, B, C  |   Reg[A] := RK[B] * RK[C]
Div,        // A, B, C  |   Reg[A] := RK[B] / RK[C]
Mod,        // A, B, C  |   Reg[A] := RK[B] % RK[C]
Pow,        // A, B, C  |   Reg[A] := RK[B} ^ RK[C]
Unm,        // A, B     |   Reg[A] := -Reg[B]
Return,     // A, B     |   return Reg[A], ... Reg[A + B - 1]
}

OpCode_Arg_Type :: enum u8 {
    Unused = 0,
    Used   = 1, // Argument is used but is not a register, constant or jump offset.
    Register,   // Or a jump offset?
    Constant,   // Or a register/constant?
}

// How should we interpret the arguments?
OpCode_Format :: enum u8 {
    Separate,    // A, B and C are all separate.
    Unsigned_BC, // B and C are combined into an unsigned 18-bit integer.
    Signed_BC,   // B and C are combined into a signed 18-bit integer.
}

// https://www.lua.org/source/5.1/lopcodes.h.html#OpArgMask
OpCode_Info :: bit_field u8 {
    type:   OpCode_Format   | 2,
    b:      OpCode_Arg_Type | 2,
    c:      OpCode_Arg_Type | 2,
    a:      OpCode_Arg_Type | 1, // Arg A can never be an RK. Just check if it's used.
}

// See: https://www.lua.org/source/5.1/lopcodes.c.html#luaP_opmodes
opcode_info := [OpCode]OpCode_Info {
.Constant   = {type = .Unsigned_BC, a = .Used, b = .Constant, c = .Unused},
.Add..=.Pow = {type = .Separate,    a = .Used, b = .Constant, c = .Constant},
.Unm        = {type = .Separate,    a = .Used, b = .Register, c = .Unused},
.Return     = {type = .Separate,    a = .Used, b = .Register, c = .Unused},
}

/* =============================================================================
Notes:
(*) Return:
    - If B == 0, then return up to the current stack top (exclusive).
============================================================================= */


// Bit 9 for arguments B and C indicates how to interpret them.
// If 0, it is a register. If 1, it is an index into the constants table.
BIT_RK :: 1 << (SIZE_B - 1)

rk_is_k :: proc(#any_int b_or_c: u16) -> bool {
    return (b_or_c & BIT_RK) != 0
}

rk_get_k :: proc(#any_int b_or_c: u16) -> u16 {
    return (b_or_c & ~cast(u16)BIT_RK)
}

rk_as_k :: proc(#any_int b_or_c: u16) -> u16 {
    return b_or_c | BIT_RK
}

// This is kinda stupid
inst_create :: proc(op: OpCode, a, b, c: u16) -> (inst: Instruction) {
    inst.b  = b
    inst.c  = c
    inst.a  = a
    inst.op = op
    return inst
}

inst_create_AB :: proc(op: OpCode, a, b: u16) -> (inst: Instruction) {
    inst.b = b
    inst.a = a
    inst.op = op
    return inst
}

inst_create_AuBC :: proc(op: OpCode, a: u16, bc: u32) -> (inst: Instruction) {
    inst.b  = cast(u16)(bc >> SIZE_C) // shift out 'c' bits
    inst.c  = cast(u16)(bc & MAX_C)   // remove 'b' bits
    inst.a  = a
    inst.op = op
    return inst
}

inst_get_uBC :: proc(inst: Instruction) -> (bc: u32) {
    bc |= cast(u32)inst.b << OFFSET_B
    bc |= cast(u32)inst.c
    return bc
}

inst_make_op :: proc(op: OpCode) -> (inst: Instruction) {
    inst.op = op
    return inst
}
