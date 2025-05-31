#+private
package lulu

SIZE_B  :: 9
SIZE_C  :: 9
SIZE_A  :: 8
SIZE_OP :: 6
SIZE_Bx :: SIZE_B + SIZE_C      // 9 + 9 = 18

MAX_B   :: (1 << SIZE_B)  - 1       // 1<<9 - 1      = 0b1_1111_1111
MAX_C   :: (1 << SIZE_C)  - 1       // 1<<9 - 1      = 0b1_1111_1111
MAX_A   :: (1 << SIZE_A)  - 1       // 1<<8 - 1      = 0b1111_1111
MAX_OP  :: (1 << SIZE_OP) - 1       // (1 << 6)  - 1 = 0b0011_1111
MAX_Bx  :: (1 << SIZE_Bx) - 1       // 1<<18 - 1     = 0b11_1111_1111_1111_1111
MAX_sBx :: MAX_Bx >> 1              //               = 0b1_1111_1111_1111_111

/*
- Format:
```
    +---------+---------+--------+-------+
    | 31..23  | 22..14  | 13..6  | 5..0  |
    +---------+---------+--------+-------+
    | B:9     | C:9     | A:8    | OP:6  |  `.Separate`
    | Bx:18             | A:8    | OP:6  |  `.Unsigned_Bx`
    | sBx:18            | A:8    | OP:6  |  `.Signed_Bx`
    +---------+---------+--------+-------+
```

- Various casts and offsets:
```
    +---------+---------+
    | 15..7   | 6..0    |
    +---------+---------+
    | B:9     | C:7     |   `u16`, byte offset 0
    +---------+---------+


    +---------+---------+--------+------+
    | 23      | 22..14  | 13..6  | 5..0 |
    +---------+---------+--------+------+
    | B:1     | C:9     | A:8    | OP:6 |   `u32`, byte offset 1
    +---------+---------+--------+------+

    +---------+--------+------+
    | 15..14  | 13..6  | 5..0 |
    +---------+--------+------+
    | C:2     | A:8    | OP:6 | `u16`, byte offset 2
    +---------+--------+------+

    +--------+------+
    | 8..6   | 5..0 |
    +--------+------+
    | A:2    | OP:6 |   `u8`, byte offset 3
    +--------+------+
```

Notes:
-   We put `B` before `C` so that it's easier to do bit manipulation to get
    the full 18-bit integer.
 */
Instruction :: bit_field u32 {
    b:  u16    | SIZE_B, // 9th bit is sign or flag
    c:  u16    | SIZE_C, // 9th bit is sign, or flag, or combined into B
    a:  u16    | SIZE_A, // Always unsigned
    op: OpCode | SIZE_OP  `fmt:"s"`,
}


/*
Links:
-   https://www.lua.org/source/5.1/lopcodes.h.html
-   https://stevedonovan.github.io/lua-5.1.4/lopcodes.h.html
 */
OpCode :: enum {
/* =============================================================================
Note on shorthand:
(*) Reg:
    -   'Register'
    -   requires absolute index into stack
    -   Reg(A) => often the destination operand
(*) Kst:
    -   'Constants Table'
    -   From current Chunk
    -   Requires absolute index
(*) RK:
    -   'Register or Constants Table'
    -   If argument B or C is >= 0x100, meaning bit 9 is toggled, then it is an
        absolute index into Kst. Otherwise, it is a Reg.
============================================================================= */
//                Args  | Description
Move,          // A B   | Reg(A) := Reg(B)
Load_Constant, // A Bx  | Reg(A) := Kst(Bx)
Load_Nil,      // A B   | Reg(i) := nil for A <= i <= B
Load_Boolean,  // A B C | Reg(1) := (Bool)B; if ((Bool)C) ip++
Get_Global,    // A Bx  | Reg(A) := _G[Kst[Bx]]
Get_Table,     // A B C | Reg(A) := Reg(B)[RK(C)]
Set_Global,    // A Bx  | _G[Kst[Bx]] := Reg(A)
Set_Table,     // A B C | Reg(A)[RK(B)] := RK(C)
New_Table,     // A B C | Reg(A) := {} ; array size = B, hash size = C
Set_Array,     // A Bx  | Reg(A)[(C-1)*FPF + i] = Reg(A + i) for 1 <= i <= B
Print,         // A B   | print(Reg(i), '\t') for A <= i < B
Add,           // A B C | Reg(A) := RK(B) + RK(C)
Sub,           // A B C | Reg(A) := RK(B) - RK(C)
Mul,           // A B C | Reg(A) := RK(B) * RK(C)
Div,           // A B C | Reg(A) := RK(B) / RK(C)
Mod,           // A B C | Reg(A) := RK(B) % RK(C)
Pow,           // A B C | Reg(A) := RK(B) ^ RK(C)
Unm,           // A B   | Reg(A) := -Reg(B)
Eq,            // A B C | if (RK(B) == RK(C)) != Bool(A) then pc++
Lt,            // A B C | if (RK(B) <  RK(C)) != Bool(A) then pc++
Leq,           // A B C | if (RK(B) <= RK(C)) != Bool(A) then pc++
Not,           // A B   | Reg(A) := not RK(B)
Concat,        // A B C | Reg(A) := Reg(A) .. Reg(i) for B <= i <= C
Len,           // A B   | Reg(A) := #Reg(B)
Test,          // A   C | if Bool(Reg(A)) != Bool(C) then pc++
Test_Set,      // A B C | if Bool(Reg(B)) != Bool(C) then pc++ else Reg(A) := Reg(B)
Jump,          // sBx   | pc += sBx
Return,        // A B C | return Reg(A), ... Reg(A + B)
}

/*
Overview:
-   Maximum value of argument B for `OpCode.Set_Array`.
-   Offset to use when decoding argument C.
-   This also means that after every `.Set_Array` instruction we can reuse these
    registers.
 */
FIELDS_PER_FLUSH :: 50

/* =============================================================================
Notes:

(*) Eq, Lt, Leq:
    -   Following the Lua 5.1.5 implementation, Argument A represents a boolean
        condition to determine how to interpret the result of the comparison.
    -   If A == 1, then comparison is kept as-is.
    -   If A == 0, then the comparison is inverted.
    -   The `pc++` business is because we expect the emit `.Load_Boolean` twice.
    -   These instructions do not modify registers by themselves.

(*) Return:
    -   If C == 1, then return up to the current stack frame top (exclusive).
    -   If C == 0, then return only up to B values (inclusive).
    -   B represents the number of return values as-is in that case.

(*) Set_Array:
    -   B represents how many values are on the top of the stack for this
        particular set list.
    -   If C == 0 then that means the *next* instruction, all its 32 bits,
        represents the actual unsigned value of C.
    -   Otherwise nonzero C represents an offset from `FIELDS_PER_FLUSH`.
    -   C will be used to calculate the actual range of indexes we need to set.
============================================================================= */


/*
**Note** (2025-05-10):
-   MUST be `u8`.
*/
OpCode_Arg_Type :: enum u8 {
    Unused = 0,
    Used,       // Argument is used but is not a register, constant or jump offset.
    Reg_Jump,   // Argument is either a register OR a jump offset?
    Reg_Const,  // Argument is either a register OR a constants table index?
}

/*
**Overview**
-   Indicates how we should interpret the arguments of an `OpCode`.

**Note** (2025-05-10):
-   MUST be `u8`, otherwise seems to result in wrong values (e.g. `.Signed_Bx`
    becomes `-2` in `.Jump` somehow)
*/
OpCode_Format :: enum u8 {
    Separate,    // A, B and C are all treated separately.
    Unsigned_Bx, // B is combined with C to form an unsigned 18-bit integer.
    Signed_Bx,   // B is combined with C to form a signed 18-bit integer.
}

// https://www.lua.org/source/5.1/lopcodes.h.html#OpArgMask
OpCode_Info :: bit_field u8 {
    type:    OpCode_Format   | 2,
    b:       OpCode_Arg_Type | 2,
    c:       OpCode_Arg_Type | 2,
    a:       bool            | 1, // Is argument A is a destination register?
    is_test: bool            | 1, // Is a comparison/test/test-set?
}


/*
**Notes**
-   In Lua, `OP_EQ`, `OP_LT` and `OP_LE` actually have the register A mode set
    to `false`.
-   This is because A is not a register; rather they are used for control flow
    and A is a constant (one of 0 or 1) to be used to determine the result of
    the comparison.

**Links**
-   https://www.lua.org/source/5.1/lopcodes.c.html#luaP_opmodes
-   https://the-ravi-programming-language.readthedocs.io/en/latest/lua_bytecode_reference.html#op-eq-op-lt-and-op-le-instructions
 */
opcode_info := [OpCode]OpCode_Info {
.Move           = {type = .Separate,    a = true,  b = .Reg_Const, c = .Unused},
.Load_Constant  = {type = .Unsigned_Bx, a = true,  b = .Reg_Const, c = .Unused},
.Load_Boolean   = {type = .Separate,    a = true,  b = .Used,      c = .Used},
.Load_Nil       = {type = .Separate,    a = true,  b = .Reg_Jump,  c = .Unused},
.Get_Global     = {type = .Unsigned_Bx, a = true,  b = .Reg_Const, c = .Unused},
.Get_Table      = {type = .Separate,    a = true,  b = .Reg_Const, c = .Reg_Const},
.Set_Global     = {type = .Unsigned_Bx, a = false, b = .Reg_Const, c = .Unused},
.Set_Table      = {type = .Separate,    a = false, b = .Reg_Const, c = .Reg_Const},
.New_Table      = {type = .Separate,    a = true,  b = .Used,      c = .Used},
.Set_Array      = {type = .Separate,    a = true,  b = .Used,      c = .Used},
.Print          = {type = .Separate,    a = true,  b = .Reg_Jump,  c = .Unused},
.Add ..= .Pow   = {type = .Separate,    a = true,  b = .Reg_Const, c = .Reg_Const},
.Unm            = {type = .Separate,    a = true,  b = .Reg_Jump,  c = .Unused},
.Eq ..= .Leq    = {type = .Separate,    a = false, b = .Reg_Const, c = .Reg_Const,  is_test = true},
.Not            = {type = .Separate,    a = true,  b = .Reg_Jump,  c = .Unused},
.Concat         = {type = .Separate,    a = true,  b = .Reg_Jump,  c = .Reg_Jump},
.Len            = {type = .Separate,    a = true,  b = .Reg_Const, c = .Unused},
.Test           = {type = .Separate,    a = false, b = .Unused,    c = .Used,       is_test = true},
.Test_Set       = {type = .Separate,    a = true,  b = .Reg_Const, c = .Used,       is_test = true},
.Jump           = {type = .Signed_Bx,   a = false, b = .Reg_Jump,  c = .Unused},
.Return         = {type = .Separate,    a = true,  b = .Used,      c = .Used},
}


// Bit 9 for arguments B and C indicates how to interpret them.
// If 0, it is a register. If 1, it is an index into the constants table.
REG_BIT_RK   :: 1 << (SIZE_B - 1)
MAX_INDEX_RK :: REG_BIT_RK - 1

reg_is_k :: #force_inline proc "contextless" (bc: u16) -> bool {
    return (bc & REG_BIT_RK) != 0
}

reg_get_k :: #force_inline proc "contextless" (bc: u16) -> u16 {
    return (bc & ~cast(u16)REG_BIT_RK)
}

reg_as_k :: #force_inline proc "contextless" (bc: u16) -> u16 {
    return bc | REG_BIT_RK
}

ip_make :: proc {
    ip_make_ABC,
    ip_make_ABx,
    ip_make_AsBx,
}

ip_make_ABC :: #force_inline proc "contextless" (op: OpCode, a, b, c: u16) -> Instruction {
    return Instruction{b = b, c = c, a = a, op = op}
}

ip_make_ABx :: #force_inline proc "contextless" (op: OpCode, a: u16, bx: u32) -> Instruction {
    return {
        b  = u16(bx >> SIZE_C), // shift out upper 'c' bits for arg B
        c  = u16(bx & MAX_C),   // remove lower 'b' bits for arg C
        a  = a,
        op = op,
    }
}

ip_make_AsBx :: #force_inline proc "contextless" (op: OpCode, a: u16, sbx: int) -> Instruction {
    return ip_make_ABx(op, a, u32(sbx + MAX_sBx))
}

ip_get_Bx :: #force_inline proc "contextless" (ip: Instruction) -> (bx: u32) {
    // NOTE(2025-05-08): Since we're dealing with bit fields rather than raw
    // unsigned integers, we don't need to use the bit index offset.
    b := u32(ip.b) << SIZE_B
    c := u32(ip.c)
    return b | c
}

ip_get_sBx :: #force_inline proc "contextless" (ip: Instruction) -> (sbx: int) {
    bx := ip_get_Bx(ip)
    return int(bx) - MAX_sBx
}

ip_set_Bx :: #force_inline proc "contextless" (ip: ^Instruction, bx: u32) {
    ip.b = u16(bx >> SIZE_C) // shift out lower `c` bits for arg B
    ip.c = u16(bx & MAX_C)   // remove upper `b` bits for arg C
}

ip_set_sBx :: #force_inline proc "contextless" (ip: ^Instruction, sbx: int) {
    ip_set_Bx(ip, u32(sbx + MAX_sBx))
}

FB_MANTISSA_SIZE    :: 3
FB_MANTISSA_IMPLIED :: 1 << FB_MANTISSA_SIZE    // 0b0000_1000
FB_MANTISSA_MASK    :: FB_MANTISSA_IMPLIED - 1  // 0b0000_0111

FB_EXPONENT_SIZE    :: 5
FB_EXPONENT_MASK    :: (1 << FB_EXPONENT_SIZE) - 1 // 0b0001_1111

/*
Overview:
-   Convert the integer `i` to an 8-bit "floating point byte".
-   The resulting float is stored in a `u8` and can only be decoded via
    `fb_to_int()`.

Format:
-   0b_eeee_exxx
-   `e`: exponent bit, `x`: mantissa bit
-   Represents the value `(0b1xxx) * 2^(0b000e_eeee - 1)`
-   The maximum value is `0b1111 * 2^(0b0001_1111 - 1)`, or `16106127360`.
    In binary that is `0b1111000000000000000000000000000000` (thanks Python!)

Analogous to:
-   `lobject.c:luaO_int2fb(unsigned int x)` in Lua 5.1.5.

Notes:
-   If `u` is greater than 15, this will likely round up a bit.
 */
fb_make :: proc(u: int) -> (fb: u8) {
    exp: u8
    u := u

    /*
    Notes:
    -   We use `0b0000_1000 << 1`, which is `16`.
    -   It is the first power of 2 that cannot be represented even with our
        implied bit of `0b0000_1000`.
    -   The maximum possible value should be `0b0000_1111`, which is 15.
     */
    for u >= (FB_MANTISSA_IMPLIED << 1) {
        // Round up
        u = (u + 1) >> 1
        exp += 1
    }

    // Exponent is 0 in this case so no need to set it
    if u < FB_MANTISSA_IMPLIED {
        return cast(u8)u
    }

    return ((exp + 1) << FB_MANTISSA_SIZE) | cast(u8)(u - FB_MANTISSA_IMPLIED)
}

/*
Overview:
-   Interprets the "floating point byte" `fb` and retrieves the positive
    integer being stored there.

Analogous to:
-   `lobject.c:luaO_fb2int(int x)` in Lua 5.1.5.
 */
fb_decode :: proc(fb: u8) -> int {
    // MUST be unsigned to allow shifing.
    exp := (fb >> FB_MANTISSA_SIZE) & FB_EXPONENT_MASK

    // No exponent, so we can just interpret the mantissa as-is
    if exp == 0 {
        return cast(int)fb
    }

    return (cast(int)(fb & FB_MANTISSA_MASK) + FB_MANTISSA_IMPLIED) << (exp - 1)
}
