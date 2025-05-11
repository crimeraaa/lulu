#+private
package lulu


Expr :: struct {
    type:        Expr_Type,
    using info:  Expr_Info,
    patch_true:  int,
    patch_false: int,
}

Expr_Info :: struct #raw_union {
    number: f64, // .Number
    pc:     int, // .Need_Register, .Jump
    index:  u32, // .Constant, .Global
    table:  struct {reg, key_reg: u16}, // .Table_Index
    reg:    u16, // .Discharged, .Local
}

/*
**Links**
-   https://the-ravi-programming-language.readthedocs.io/en/latest/lua-parser.html#state-transitions
 */
Expr_Type :: enum {
    Empty,          // Zero-value or no arguments. See: `VVOID`.
    Discharged,     // ^Expr was emitted to a register. See: `VNONRELOC`.
    Need_Register,  // ^Expr needs to be assigned to a register. See: `VRELOCABLE`.
    Nil,
    True,
    False,
    Number,
    Constant,
    Global,
    Local,
    Table_Index,
    Jump,
}

// Intended to be easier to grep
// Inspired by: https://www.lua.org/source/5.1/lparser.c.html#simpleexp
expr_make :: proc {
    expr_make_none,
    expr_make_pc,
    expr_make_reg,
    expr_make_index,
    expr_make_table,
    expr_make_number,
}

expr_make_none :: proc(type: Expr_Type) -> Expr {
    return Expr{
        type        = type,
        patch_true  = NO_JUMP,
        patch_false = NO_JUMP,
    }
}

expr_make_pc :: proc(type: Expr_Type, pc: int) -> Expr {
    assert(type == .Need_Register)
    return Expr{
        type        = type,
        pc          = pc,
        patch_true  = NO_JUMP,
        patch_false = NO_JUMP,
    }
}

expr_make_reg :: proc(type: Expr_Type, reg: u16) -> Expr {
    assert(type == .Discharged || type == .Local)
    return Expr{
        type        = type,
        reg         = reg,
        patch_true  = NO_JUMP,
        patch_false = NO_JUMP,
    }
}

expr_make_index :: proc(type: Expr_Type, index: u32) -> Expr {
    assert(type == .Global || type == .Constant)
    return Expr{
        type        = type,
        index       = index,
        patch_true  = NO_JUMP,
        patch_false = NO_JUMP,
    }
}

/*
**Assumptions**
-   `reg` contains the register of the table.
-   `index` contains the register/constant of the table's index/key.

**Guarantees**
-   `expr.table` will be filled using the above information.
 */
expr_make_table :: proc(type: Expr_Type, reg, index: u16) -> Expr {
    assert(type == .Table_Index)
    return Expr{
        type        = .Table_Index,
        table       = {reg = reg, key_reg = index},
        patch_true  = NO_JUMP,
        patch_false = NO_JUMP,
    }
}

expr_make_number :: proc(type: Expr_Type, number: f64) -> Expr {
    assert(type == .Number)
    return Expr{
        type        = .Number,
        number      = number,
        patch_true  = NO_JUMP,
        patch_false = NO_JUMP,
    }
}

// See: https://www.lua.org/source/5.1/lcode.c.html#isnumeral
expr_is_number :: proc(e: Expr) -> bool {
    return e.type == .Number && e.patch_true == NO_JUMP && e.patch_false == NO_JUMP
}

expr_has_jumps :: proc(e: Expr) -> bool {
    return e.patch_true != e.patch_false
}

