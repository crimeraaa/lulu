#+private
package lulu

import "core:fmt"
import "core:math"

Value :: struct {
    type        : Value_Type,
    using data  : Value_Data,
}

Value_Data :: struct #raw_union {
    number  :  f64,
    boolean :  bool,
    ostring : ^OString,
    table   : ^Table,
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
Number_Proc :: #type proc(a, b: f64) -> f64

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
number_mod :: #force_inline proc(a, b: f64) -> f64 {
    return a - math.floor(a / b)*b
}

number_pow :: #force_inline proc(a, b: f64) -> f64 {
    return math.pow(a, b)
}

number_eq :: #force_inline proc(a, b: f64) -> bool {
    return a == b
}

number_lt :: #force_inline proc(a, b: f64) -> bool {
    return a < b
}

number_gt :: #force_inline proc(a, b: f64) -> bool {
    return a > b
}

number_leq :: #force_inline proc(a, b: f64) -> bool {
    return a <= b
}

number_geq :: #force_inline proc(a, b: f64) -> bool {
    return a >= b
}

number_unm :: #force_inline proc(a: f64) -> f64 {
    return -a
}

value_type_name :: #force_inline proc(v: Value) -> string {
    return value_type_strings[v.type]
}

value_make_nil :: #force_inline proc() -> Value {
    return Value{type = .Nil, number = 0}
}

value_make_boolean :: #force_inline proc(b: bool) -> Value {
    return Value{type = .Boolean, boolean = b}
}

value_make_number :: #force_inline proc(n: f64) -> Value {
    return Value{type = .Number, number = n}
}

value_make_string :: #force_inline proc(str: ^OString) -> Value {
    return Value{type = .String, ostring = str}
}

value_set_nil :: proc(v: ^Value) {
    v.type   = .Nil
    v.number = 0
}

value_set_boolean :: proc(v: ^Value, b: bool) {
    v.type    = .Boolean
    v.boolean = b
}

value_set_number :: proc(v: ^Value, n: f64) {
    v.type   = .Number
    v.number = n
}

value_set_string :: proc(v: ^Value, str: ^OString) {
    v.type    = .String
    v.ostring = str
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

number_is_nan :: math.is_nan

value_print :: proc(value: Value, mode := Value_Print_Mode.Normal) {
    // We assume this is valid for numbers, booleans and pointers.
    buf: [64]byte
    s := value_to_string(buf[:], value)
    switch mode {
    case .Normal:
        fmt.println(s)
    case .Debug:
        if value_is_string(value) {
            fmt.printfln("%q", s)
        } else {
            fmt.println(s)
        }
    case .Stack:
        fmt.printf("[ %s ]", s)
    case .Print:
        fmt.printf("%s\t", s)
    }
}

value_to_string :: proc(buf: []byte, value: Value) -> string {
    switch value.type {
    case .Nil:      return "nil"
    case .Boolean:  return "true" if value.data.boolean else "false"
    case .Number:   return fmt.bprintf(buf, "%.14g", value.data.number)
    case .String:   return ostring_to_string(value.ostring)
    case .Table:    return fmt.bprintf(buf, "%s: %p", value_type_name(value), cast(rawptr)value.table)
    }
    unreachable()
}

