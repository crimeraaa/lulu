#+private
package lulu

import "core:fmt"
import "core:io"
import "core:math"

Value :: struct {
    type:       Value_Type,
    using data: Value_Data,
}

Value_Data :: struct #raw_union {
    number:   f64,
    boolean:  bool,
    ostring: ^OString,
    table:   ^Table,
}

Value_Type :: enum {
    Nil,
    Number,
    Boolean,
    String,
    Table,
}

Value_Print_Mode :: enum u8 {
    Normal, // Prints the value as-is and prints a newline.
    Debug,  // Same as .Normal, but surrounds strings with quotes.
    Stack,  // Prints the value surrounded by square brackets. No newline.
    Print,  // Don't print a newline. Prints a tab character after.
}

@(rodata)
value_type_strings := [Value_Type]string {
    .Nil     = "nil",
    .Boolean = "boolean",
    .Number  = "number",
    .String  = "string",
    .Table   = "table",
}

// Used for callbacks/dispatches
Number_Arith_Proc   :: #type proc(a, b: f64) -> f64
Number_Compare_Proc :: #type proc(a, b: f64) -> bool

number_add :: proc(a, b: f64) -> f64 {
    return a + b
}

number_sub :: proc (a, b: f64) -> f64 {
    return a - b
}

number_mul :: proc(a, b: f64) -> f64 {
    return a * b
}

number_div :: proc(a, b: f64) -> f64 {
    return a / b
}

/*
Links:
-   https://www.lua.org/source/5.1/luaconf.h.html#luai_nummod
 */
number_mod :: proc(a, b: f64) -> f64 {
    return a - math.floor(a / b)*b
}

number_pow :: proc(a, b: f64) -> f64 {
    return math.pow(a, b)
}

number_eq :: proc(a, b: f64) -> bool {
    return a == b
}

number_lt :: proc(a, b: f64) -> bool {
    return a < b
}

number_gt :: proc(a, b: f64) -> bool {
    return a > b
}

number_leq :: proc(a, b: f64) -> bool {
    return a <= b
}

number_geq :: proc(a, b: f64) -> bool {
    return a >= b
}

number_unm :: proc(a: f64) -> f64 {
    return -a
}

value_make :: proc {
    value_make_nil,
    value_make_boolean,
    value_make_number,
    value_make_integer,
    value_make_string,
    value_make_table,
}

value_type_name :: proc(v: Value) -> string {
    return value_type_strings[v.type]
}

value_make_nil :: proc() -> Value {
    return Value{type = .Nil, number = 0}
}

value_make_boolean :: proc(b: bool) -> Value {
    return Value{type = .Boolean, boolean = b}
}

value_make_number :: proc(n: f64) -> Value {
    return Value{type = .Number, number = n}
}

value_make_integer :: proc(i: int) -> Value {
    return value_make_number(cast(f64)i)
}

value_make_string :: proc(s: ^OString) -> Value {
    return Value{type = .String, ostring = s}
}

value_make_table :: proc(t: ^Table) -> Value {
    return Value{type = .Table, table = t}
}

value_is_nil :: proc(a: Value) -> bool {
    return a.type == .Nil
}

value_is_boolean :: proc(a: Value) -> bool {
    return a.type == .Boolean
}

value_is_falsy :: proc(a: Value) -> bool {
    return a.type == .Nil || (a.type == .Boolean && !a.boolean)
}

value_is_number :: proc(a: Value) -> bool {
    return a.type == .Number
}

value_is_string :: proc(a: Value) -> bool {
    return a.type == .String
}

value_is_table :: proc(a: Value) -> bool {
    return a.type == .Table
}

// Utility function because this is so common.
value_to_string :: proc(a: Value) -> string {
    assert(value_is_string(a))
    return ostring_to_string(a.ostring)
}

value_eq :: proc(a, b: Value) -> bool {
    if a.type != b.type {
        return false
    }

    switch a.type {
    case .Nil:      return true
    case .Boolean:  return a.boolean == b.boolean
    case .Number:   return number_eq(a.number, b.number)
    case .String:   return a.ostring == b.ostring
    case .Table:    return a.table == b.table
    }
    unreachable()
}

number_is_nan :: math.is_nan_f64

value_formatter :: proc(info: ^fmt.Info, arg: any, verb: rune) -> bool {
    value  := (cast(^Value)arg.data)^
    writer := info.writer
    switch verb {
    case 'v':
        switch value.type {
        case .Nil:
            io.write_string(writer, "nil", &info.n)
        case .Boolean:
            io.write_string(writer, "true" if value.boolean else "false", &info.n)
        case .Number:
            info.n += fmt.wprintf(writer, "%.14g", value.number)
        case .String:
            io.write_string(writer, value_to_string(value))
        case .Table:
            type_name := value_type_name(value)
            pointer   := cast(rawptr)value.table
            info.n += fmt.wprintf(writer, "%s: %p", type_name, pointer)
        case:
            unreachable()
        }
        return true
    case:
        return false
    }
}

value_print :: proc(value: Value, mode := Value_Print_Mode.Normal) {
    switch mode {
    case .Normal:
        fmt.println(value)
    case .Debug:
        if value_is_string(value) {
            fmt.printf("%q", value.ostring)
        } else {
            fmt.print(value)
        }
    case .Stack:
        if value_is_string(value) {
            fmt.printf("\t[ %q ]", value.ostring)
        } else {
            fmt.printf("\t[ %v ]", value)
        }
    case .Print:
        fmt.print(value, '\t')
    }
}

