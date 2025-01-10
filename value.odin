#+private
package lulu

import "core:fmt"
import "core:math"

Value :: struct {
    type: Value_Type,
    data: Value_Data,
}

Value_Data :: struct #raw_union {
    number:  f64,
    boolean: bool,
}

Value_Type :: enum {
    Nil,
    Number,
    Boolean,
}

Value_Print_Mode :: enum u8 {
    Normal, // Prints the value as-is and prints a newline.
    Debug,  // Same as .Normal, but surrounds strings with quotes.
    Stack,  // Prints the value surrounded by square brackets. No newline.
}

@(rodata)
value_type_strings := [Value_Type]string {
    .Nil     = "nil",
    .Boolean = "boolean",
    .Number  = "number",
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

value_make_nil :: proc() -> Value {
    return Value{type = .Nil, data = {number = 0}}
}

value_make_boolean :: proc(b: bool) -> Value {
    return Value{type = .Boolean, data = {boolean = b}}
}

value_make_number :: proc(n: f64) -> Value {
    return Value{type = .Number, data = {number = n}}
}

value_set_nil :: proc(v: ^Value) {
    v.type         = .Nil
    v.data.number = 0
}

value_set_boolean :: proc(v: ^Value, b: bool) {
    v.type          = .Boolean
    v.data.boolean = b
}

value_set_number :: proc(v: ^Value, n: f64) {
    v.type         = .Number
    v.data.number = n
}

value_is_nil :: proc(a: Value) -> bool {
    return a.type == .Nil
}

value_is_boolean :: proc(a: Value) -> bool {
    return a.type == .Boolean
}

value_is_falsy :: proc(a: Value) -> bool {
    return a.type == .Nil || (a.type == .Boolean && !a.data.boolean)
}

value_is_number :: proc(a: Value) -> bool {
    return a.type == .Number
}

value_eq :: proc(a, b: Value) -> bool {
    if a.type != b.type {
        return false
    }
    switch a.type {
    case .Nil:      return true
    case .Boolean:  return a.data.boolean == b.data.boolean
    case .Number:   return number_eq(a.data.number, b.data.number)
    }
    unreachable()
}

number_is_nan :: math.is_nan

value_print :: proc(value: Value, mode := Value_Print_Mode.Normal) {
    // We assume this is valid for numbers, booleans and pointers.
    buf: [64]byte
    s := value_to_string(buf[:], value)
    switch mode {
    case .Normal:   fallthrough
    case .Debug:    fmt.println(s)
    case .Stack:    fmt.printf("[ %s ]", s)
    }
}

value_to_string :: proc(buf: []byte, value: Value) -> string {
    switch value.type {
    case .Nil:      return "nil"
    case .Boolean:  return "true" if value.data.boolean else "false"
    case .Number:   return fmt.bprintf(buf, "%.14g", value.data.number)
    }
    unreachable()
}

