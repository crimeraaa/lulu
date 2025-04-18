#+private
package lulu


/*
**Notes**
-   On x86-64, this should be 16 bytes due to 8-byte alignment from the `f64`.
-   This means it should be able to be passed/returned in 2 registers if the
    calling convention allows it.
 */
Expr :: struct {
    type:       Expr_Type,
    using info: Expr_Info,
}

Expr_Info :: struct #raw_union {
    number: f64, // .Number
    pc:     int, // .Need_Register
    index:  u32, // .Constant, .Global
    table:  Expr_Table, // .Table_Index
    reg:    u16, // .Discharged, .Local
}

Expr_Table :: struct {
    reg, index: u16,
}


/*
**Links**
-   https://the-ravi-programming-language.readthedocs.io/en/latest/lua-parser.html#state-transitions
 */
Expr_Type :: enum u8 {
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

expr_make_none :: proc(type: Expr_Type) -> (expr: Expr) {
    return Expr{type = type}
}

expr_make_pc :: proc(type: Expr_Type, pc: int) -> (expr: Expr) {
    assert(type == .Need_Register)
    return Expr{type = type, pc = pc}
}

expr_make_reg :: proc(type: Expr_Type, reg: u16) -> (expr: Expr) {
    assert(type == .Discharged || type == .Local)
    return Expr{type = type, reg = reg}
}

expr_make_index :: proc(type: Expr_Type, index: u32) -> (expr: Expr) {
    assert(type == .Global || type == .Constant)
    return Expr{type = type, index = index}
}

/*
**Assumptions**
-   `reg` contains the register of the table.
-   `index` contains the register/constant of the table's index/key.

**Guarantees**
-   `expr.table` will be filled using the above information.
 */
expr_make_table :: proc(type: Expr_Type, reg, index: u16) -> (expr: Expr) {
    assert(type == .Table_Index)
    return Expr{type = .Table_Index, table = {reg = reg, index = index}}
}

expr_make_number :: proc(type: Expr_Type, number: f64) -> (expr: Expr) {
    assert(type == .Number)
    return Expr{type = .Number, number = number}
}

// NOTE: In the future, may need to check for jump lists!
// See: https://www.lua.org/source/5.1/lcode.c.html#isnumeral
expr_is_number :: proc(expr: Expr) -> bool {
    return expr.type == .Number
}
