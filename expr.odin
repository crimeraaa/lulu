#+private
package lulu

import "core:strings"
import "core:fmt"


/*
Notes:
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
    reg:    u16, // .Discharged, .Local
    table:  struct {reg, index: u16}, // .Table_Index
    index:  u32, // .Constant, .Global
    pc:     int, // .Need_Register
}

/*
Links:
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
expr_init :: proc(expr: ^Expr, type: Expr_Type) {
    expr.type = type
}

expr_set_pc :: proc(expr: ^Expr, type: Expr_Type, pc: int) {
    assert(type == .Need_Register)
    expr.type = type
    expr.pc   = pc
}

expr_set_reg :: proc(expr: ^Expr, type: Expr_Type, reg: u16) {
    assert(type == .Discharged || type == .Local)
    expr.type = type
    expr.reg  = reg
}

expr_set_index :: proc(expr: ^Expr, type: Expr_Type, index: u32) {
    assert(type == .Global || type == .Constant)
    expr.type  = type
    expr.index = index
}

/*
Assumptions:
-   `expr.reg` contains the register of the table.
-   `index` contains the register/constant of the table's index/key.

Guarantees:
-   `expr.table` will be filled using the above information.
 */
expr_set_table :: proc(expr: ^Expr, type: Expr_Type, index: u16) {
    assert(type == .Table_Index)
    reg := expr.reg
    expr.type        = type
    expr.table.reg   = reg
    expr.table.index = index
}

expr_set_number :: proc(expr: ^Expr, n: f64) {
    expr.type   = .Number
    expr.number = n
}

expr_set_boolean :: proc(expr: ^Expr, b: bool) {
    expr.type = .True if b else .False
}

// NOTE: In the future, may need to check for jump lists!
// See: https://www.lua.org/source/5.1/lcode.c.html#isnumeral
expr_is_number :: proc(expr: Expr) -> bool {
    return expr.type == .Number
}

// The returned string will not last the next call to this!
expr_to_string :: proc(expr: ^Expr) -> string {
    @(thread_local)
    buf: [64]byte

    builder := strings.builder_from_bytes(buf[:])
    fmt.sbprint(&builder, "{info = {")
    #partial switch expr.type {
    case .Nil, .True, .False:
    case .Number:           fmt.sbprintf(&builder, "number = %f", expr.number)
    case .Need_Register:    fmt.sbprintf(&builder, "pc = %i", expr.pc)
    case .Discharged:       fmt.sbprintf(&builder, "reg = %i", expr.reg)
    case .Constant:         fmt.sbprintf(&builder, "index = %i", expr.index)
    case:                   unreachable()
    }
    fmt.sbprintf(&builder, "}, type = %s}", expr.type)
    return strings.to_string(builder)
}
