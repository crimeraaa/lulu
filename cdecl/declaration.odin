package cdecl

Prefix :: enum u8 {
    None, Struct, Enum, Union
}

Modifier :: enum u8 {
    None, Signed, Unsigned, Complex
}

Qualifier :: enum u8 {
    Const, Volatile
}

Tag :: enum u8 {
    None, Void, Bool,
    Char, Short, Int, Long, Long_Long,
    Float, Double, Long_Double,
}

Qualifier_Set :: distinct bit_set[Qualifier]

Declaration :: struct {
    tag:        Tag, // Required if `prefix` is `.None`
    prefix:     Prefix,
    modifier:   Modifier,
    qualifiers: Qualifier_Set,
    ident:      string, // required if `prefix` is not `.None`
    pointee:   ^Declaration,
}

to_string :: proc {
    token_to_string,
    prefix_to_string,
    modifier_to_string,
    qualifier_to_string,
    tag_to_string,
}

prefix_to_string :: #force_inline proc "contextless" (prefix: Prefix) -> string {
    @(static, rodata)
    prefix_strings := [Prefix]string{
        .None   = "none",
        .Struct = "struct",
        .Enum   = "enum",
        .Union  = "union",
    }
    return prefix_strings[prefix]
}

modifier_to_string :: #force_inline proc "contextless" (mod: Modifier) -> string {
    @(static, rodata)
    modifier_strings := [Modifier]string{
        .None     = "none",
        .Signed   = "signed",
        .Unsigned = "unsigned",
        .Complex  = "complex",
    }
    return modifier_strings[mod]
}

qualifier_to_string :: #force_inline proc "contextless" (qual: Qualifier) -> string {
    @(static, rodata)
    qualifier_strings := [Qualifier]string {
        .Const    = "const",
        .Volatile = "volatile",
    }
    return qualifier_strings[qual]
}

tag_is_integer :: #force_inline proc "contextless" (tag: Tag) -> bool {
    return .Char <= tag && tag <= .Long_Long
}

tag_is_floating :: #force_inline proc "contextless" (tag: Tag) -> bool {
    return .Float <= tag && tag <= .Long_Double
}

tag_to_string :: #force_inline proc "contextless" (tag: Tag) -> string {
    @(static, rodata)
    tag_strings := [Tag]string {
        .None        =  "none",
        .Void        =  "void",
        .Bool        =  "bool",
        .Char        =  "char",
        .Short       =  "short",
        .Int         =  "int",
        .Long        =  "long",
        .Long_Long   =  "long long",
        .Float       =  "float",
        .Double      =  "double",
        .Long_Double =  "long double",
    }
    return tag_strings[tag]
}

declaration_make :: proc(pointee: ^Declaration = nil) -> Declaration {
    return Declaration{pointee = pointee}
}
