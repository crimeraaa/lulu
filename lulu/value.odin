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

// Used for callbacks/dispatches
Number_Arith_Proc   :: #type proc "contextless" (a, b: f64) -> f64
Number_Compare_Proc :: #type proc "contextless" (a, b: f64) -> bool

number_add :: #force_inline proc "contextless" (a, b: f64) -> f64 {
    return a + b
}

number_sub :: #force_inline proc "contextless"  (a, b: f64) -> f64 {
    return a - b
}

number_mul :: #force_inline proc "contextless" (a, b: f64) -> f64 {
    return a * b
}

number_div :: #force_inline proc "contextless" (a, b: f64) -> f64 {
    return a / b
}

/*
Links:
-   https://www.lua.org/source/5.1/luaconf.h.html#luai_nummod
 */
number_mod :: #force_inline proc "contextless" (a, b: f64) -> f64 {
    return a - math.floor(a / b)*b
}

number_pow :: #force_inline proc "contextless" (a, b: f64) -> f64 {
    return math.pow(a, b)
}

number_eq :: #force_inline proc "contextless" (a, b: f64) -> bool {
    return a == b
}

number_lt :: #force_inline proc "contextless" (a, b: f64) -> bool {
    return a < b
}

number_gt :: #force_inline proc "contextless" (a, b: f64) -> bool {
    return a > b
}

number_leq :: #force_inline proc "contextless" (a, b: f64) -> bool {
    return a <= b
}

number_geq :: #force_inline proc "contextless" (a, b: f64) -> bool {
    return a >= b
}

number_unm :: #force_inline proc "contextless" (a: f64) -> f64 {
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

value_type_name :: #force_inline proc "contextless" (v: Value) -> string {
    @(static, rodata)
    value_type_strings := [Value_Type]string {
        .Nil     = "nil",
        .Boolean = "boolean",
        .Number  = "number",
        .String  = "string",
        .Table   = "table",
    }
    return value_type_strings[v.type]
}

value_make_nil :: #force_inline proc "contextless" () -> Value {
    return Value{type = .Nil, number = 0}
}

value_make_boolean :: #force_inline proc "contextless" (b: bool) -> Value {
    return Value{type = .Boolean, boolean = b}
}

value_make_number :: #force_inline proc "contextless" (n: f64) -> Value {
    return Value{type = .Number, number = n}
}

value_make_integer :: #force_inline proc "contextless" (i: int) -> Value {
    return value_make_number(cast(f64)i)
}

value_make_string :: #force_inline proc "contextless" (s: ^OString) -> Value {
    return Value{type = .String, ostring = s}
}

value_make_table :: #force_inline proc "contextless" (t: ^Table) -> Value {
    return Value{type = .Table, table = t}
}

value_is_nil :: #force_inline proc "contextless" (v: Value) -> bool {
    return v.type == .Nil
}

value_is_boolean :: #force_inline proc "contextless" (v: Value) -> bool {
    return v.type == .Boolean
}

value_is_falsy :: #force_inline proc "contextless" (v: Value) -> bool {
    return v.type == .Nil || (v.type == .Boolean && !v.boolean)
}

value_is_number :: #force_inline proc "contextless" (v: Value) -> bool {
    return v.type == .Number
}

value_is_string :: #force_inline proc "contextless" (v: Value) -> bool {
    return v.type == .String
}

value_is_table :: #force_inline proc "contextless" (v: Value) -> bool {
    return v.type == .Table
}

// Utility function because this is so common.
value_to_string :: #force_inline proc "contextless" (v: Value) -> string {
    assert_contextless(value_is_string(v))
    return ostring_to_string(v.ostring)
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
    unreachable("Unknown value type %v", a.type)
}

number_is_nan :: math.is_nan_f64

value_formatter :: proc(info: ^fmt.Info, arg: any, verb: rune) -> bool {
    v := (cast(^Value)arg.data)^
    w := info.writer
    switch verb {
    // Normal
    case 'v':
        switch v.type {
        case .Nil:
            io.write_string(w, "nil", &info.n)
        case .Boolean:
            io.write_string(w, "true" if v.boolean else "false", &info.n)
        case .Number:
            info.n += fmt.wprintf(w, "%.14g", v.number)
        case .String:
            io.write_string(w, value_to_string(v), &info.n)
        case .Table:
            type_name := value_type_name(v)
            pointer   := cast(rawptr)v.table
            info.n += fmt.wprintf(w, "%s: %p", type_name, pointer)
        case:
            unreachable("Unknown value type %v", v.type)
        }

    // Debug
    case 'd':
        if value_is_string(v) {
            // Delegate to `ostring_formatter()`
            info.n += fmt.wprintf(w, "%q", v.ostring)
        } else {
            // Delegate to normal case
            info.n += fmt.wprint(w, v)
        }

    // Stack (not string!)
    case 's':
        // Delegate to Debug case
        info.n += fmt.wprintf(w, "\t[ %d ]", v)

    case:
        return false
    }
    return true
}
