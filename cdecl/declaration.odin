package cdecl

Modifier :: enum u8 {
    None, Signed, Unsigned, Complex
}

Qualifier :: enum u8 {
    Const, Volatile
}

Base_Tag :: enum u8 {
    None, Void, Bool,
    Char, Short, Int, Long, Long_Long,
    Float, Double, Long_Double,
    Struct, Enum, Union,
    Pointer,
}

Qualifier_Set :: distinct bit_set[Qualifier]

Declaration :: struct {
    base_tag:   Base_Tag        `fmt:"s"`, // Required if `prefix` is `.None`
    modifier:   Modifier,
    qualifiers: Qualifier_Set,
    tag, var:   string          `fmt:"q"`,
    child:     ^Declaration     `fmt:"-"`,
}

to_string :: proc {
    token_to_string,
    modifier_to_string,
    qualifier_to_string,
    tag_to_string,
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

tag_to_string :: #force_inline proc "contextless" (tag: Base_Tag) -> string {
    // Can't directly map to `Token_Type` because there is no `long long` and
    // `long double` token.
    @(static, rodata)
    tag_strings := [Base_Tag]string {
        .None        = "none",
        .Void        = "void",
        .Bool        = "bool",
        .Char        = "char",
        .Short       = "short",
        .Int         = "int",
        .Long        = "long",
        .Long_Long   = "long long",
        .Float       = "float",
        .Double      = "double",
        .Long_Double = "long double",
        .Struct      = "struct",
        .Enum        = "enum",
        .Union       = "union",
        .Pointer     = "<pointer>",
    }
    return tag_strings[tag]
}

decl_make :: proc(child: ^Declaration = nil) -> Declaration {
    return Declaration{
        base_tag = .Pointer if child != nil else .None,
        child     = child,
    }
}

decl_destroy :: proc(self: ^Declaration) {
    for child := self.child; child != nil; {
        next := child.child
        free(child)
        child = next
    }
}

decl_set_pointer :: proc(#no_alias self, pointee: ^Declaration) {
    // Very important for `self` and `pointee` to be 2 unique addresses as we
    // will reset `self`!
    pointee^ = self^
    self^    = decl_make(pointee)
}

decl_is_integer :: #force_inline proc "contextless" (self: ^Declaration) -> bool {
    return .Char <= self.base_tag && self.base_tag <= .Long_Long
}

decl_is_floating :: #force_inline proc "contextless" (self: ^Declaration) -> bool {
    return .Float <= self.base_tag && self.base_tag <= .Long_Double
}

