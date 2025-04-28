package cdecl

import c "core:c/libc"
import "core:fmt"
import "core:io"

Parser :: struct {
    tokens:   []Token,
    current:    Token,
    cursor:     int,
    primary:   ^Declaration,
    writer:     io.Writer,
    try_block: ^Try_Block   `fmt:"-"`,
}

Try_Block :: struct {
    prev: ^Try_Block,
    impl:  c.jmp_buf,
}

parser_make :: proc(tokens: []Token, writer: io.Writer) -> Parser {
    return Parser{tokens = tokens, writer  = writer}
}


/*
**Notes** (2025-04-28):
-   If an error is thrown, the message will be written to `self.writer`.
-   E.g. if it wraps a `strings.Builder` then call `strings.to_string()` to
    extract the message.
*/
parser_parse :: proc(self: ^Parser, decl: ^Declaration) -> bool {
    self.primary         = decl
    self.try_block       = &Try_Block{prev = self.try_block}
    defer self.try_block = self.try_block.prev

    if c.setjmp(&self.try_block.impl) == 0 {
        parser_advance(self)
        parser_decl(self, decl)
        return true
    }
    return false
}

parser_decl :: proc(self: ^Parser, decl: ^Declaration) {
    for self.current.type != .Eof {
        parser_loop(self, decl)
    }
    parser_canonicalize(self, decl)
}

parser_loop :: proc(self: ^Parser, decl: ^Declaration) {

    set_tag :: proc(self: ^Parser, decl: ^Declaration, tag: Base_Tag) {
        if decl.base_tag == .None {
            decl.base_tag = tag
            return
        }
        have_tag(self, decl, tag)
    }

    have_tag :: proc(self: ^Parser, decl: ^Declaration, got: Base_Tag) -> ! {
        parser_throw(self, "Cannot combine tags %q and %q",
                     to_string(decl.base_tag), to_string(got))
    }

    set_modifier :: proc(self: ^Parser, decl: ^Declaration, mod: Modifier) {
        if decl.modifier == .None {
            decl.modifier = mod
            return
        }

        if decl.modifier == mod {
            parser_throw(self, "Duplicate modifier %q", to_string(mod))
        } else {
            parser_throw(self, "Cannot combine modifers %q and %q",
                         to_string(decl.modifier), to_string(mod))
        }
    }

    set_qualifier :: proc(self: ^Parser, decl: ^Declaration, qual: Qualifier) {
        if qual not_in decl.qualifiers {
            // `qual` itself needs to be contained in a bit-set in order to get
            // the union of 2 sets.
            decl.qualifiers |= {qual}
            return
        }
        parser_throw(self, "Duplicate qualifier %q", to_string(qual))
    }

    set_prefix :: proc(self: ^Parser, decl: ^Declaration, prefix: Base_Tag) {
        if decl.base_tag == .None {
            decl.base_tag = prefix
            parser_consume(self, .Ident)
            decl.tag = self.current.lexeme
            return
        }
        parser_throw(self, "Cannot combine %q with %q",
                     to_string(decl.base_tag), self.current.lexeme)
    }


    // The following is an absolute monstrosity
    #partial switch self.current.type {
    case .Void: set_tag(self, decl, .Void)
    case .Bool: set_tag(self, decl, .Bool)
    case .Char: set_tag(self, decl, .Char)

    // Integers
    case .Short:
        #partial switch decl.base_tag {
        case .None: fallthrough // `short`
        case .Int:  decl.base_tag = .Short // `int short`
        case: have_tag(self, decl, .Short)
        }
    case .Int:
        #partial switch decl.base_tag {
        case .None:      decl.base_tag = .Int       // `int`
        case .Short:     decl.base_tag = .Short     // `short int`
        case .Long:      decl.base_tag = .Long      // `long int`
        case .Long_Long: decl.base_tag = .Long_Long // `long long int`
        case: have_tag(self, decl, .Int)
        }
    case .Long:
        #partial switch decl.base_tag {
        case .None:   fallthrough             // `long`
        case .Int:    decl.base_tag = .Long        // `int long`
        case .Long:   decl.base_tag = .Long_Long   // `long long`, `int long long`
        case .Double: decl.base_tag = .Long_Double // `double long`
        case: have_tag(self, decl, .Long)
        }

    // Floating-point
    case .Float:
        set_tag(self, decl, .Float)
    case .Double:
        #partial switch decl.base_tag {
        case .None: decl.base_tag = .Double      // `double`
        case .Long: decl.base_tag = .Long_Double // `long double`
        case: have_tag(self, decl, .Double)
        }

    // Prefixes
    case .Struct:   set_prefix(self, decl, .Struct)
    case .Enum:     set_prefix(self, decl, .Enum)
    case .Union:    set_prefix(self, decl, .Union)

    // Modifiers
    case .Signed:   set_modifier(self, decl, .Signed)
    case .Unsigned: set_modifier(self, decl, .Unsigned)
    case .Complex:  set_modifier(self, decl, .Complex)

    // Qualifiers
    case .Const:    set_qualifier(self, decl, .Const)
    case .Volatile: set_qualifier(self, decl, .Volatile)

    case .Asterisk:
        decl_set_pointer(decl, new(Declaration))
        parser_canonicalize(self, decl.child)

    case .Ident:
        if var := decl.var; var != "" {
            parser_throw(self, "Already have identifier %q, got %q", var,
                         self.current.lexeme)
        }
        decl.var = self.current.lexeme

    case:
        parser_throw(self, "Unexpected token: %q", self.current.lexeme)
    }
    
    parser_advance(self)
}

parser_canonicalize :: proc(self: ^Parser, decl: ^Declaration) {
    bad_modifier :: proc(self: ^Parser, decl: ^Declaration) -> ! {
        parser_throw(self, "Cannot use %q on %q", to_string(decl.modifier),
                     to_string(decl.base_tag))
    }

    // Need to deduce default types for lone modifiers?
    if decl.base_tag == .None {
        switch decl.modifier {
        case .None: parser_throw(self, "Got no tag nor modifier")
        case .Signed:   fallthrough         // `signed` == `int`
        case .Unsigned: decl.base_tag = .Int     // `unsigned` == `unsigned int`
        case .Complex:  decl.base_tag = .Double  // `complex` == `double complex`
        }
    }

    // Verify that all modifiers are paired with appropriate tags.
    #partial switch decl.modifier {
    case .None:
        // `char` is not guaranteed to be signed nor unsigned.
        if .Short <= decl.base_tag && decl.base_tag <= .Long_Long {
            decl.modifier = .Signed
        }
    case .Signed, .Unsigned:
        if decl_is_integer(decl) {
            break
        }
        bad_modifier(self, decl)
    case .Complex:
        if decl_is_floating(decl) {
            break
        }
        // `long complex` or `complex long`
        if decl.base_tag == .Long {
            decl.base_tag = .Long_Double
            break
        }
        bad_modifier(self, decl)
    }
}

parser_dump :: proc(self: ^Parser, decl: ^Declaration = nil, counter := 1) {
    decl := self.primary if decl == nil else decl
    w    := self.writer
    
    fmt.wprintf(w, "[%i] ", counter)
    
    if decl.var != "" {
        fmt.wprintf(w, "%q: ", decl.var)
    }

    for qual in decl.qualifiers {
        fmt.wprintf(w, "%s ", to_string(qual))
    }
    
    switch mod := decl.modifier; mod {
    case .None, .Complex:
        break
    case .Signed:
        if decl.base_tag != .Char {
            break
        }
        fallthrough
    case .Unsigned:
        fmt.wprintf(w, "%s ", to_string(mod))
    }
    
    fmt.wprintf(w, "%s", to_string(decl.base_tag))
    
    if mod := decl.modifier; mod == .Complex {
        fmt.wprintf(w, " %s", to_string(mod))
    }

    if decl.tag != "" {
        fmt.wprintf(w, " %s", decl.tag)
    }
    
    if decl.child != nil {
        fmt.wprintln(w)
        parser_dump(self, decl.child, counter + 1)
    }
}

parser_advance :: proc(self: ^Parser) {
    if self.cursor < len(self.tokens) {
        self.current = self.tokens[self.cursor]
        self.cursor += 1
    }
}

parser_match :: proc(self: ^Parser, expected: Token_Type) -> bool {
    token := self.tokens[self.cursor]
    found := token.type == expected
    if found {
        self.current = token
        self.cursor += 1
    }
    return found
}

parser_consume :: proc(self: ^Parser, expected: Token_Type) {
    token := self.tokens[self.cursor]
    if token.type != expected {
        parser_throw(self, "Expected %q, got %q", to_string(expected),
        token.lexeme)
    }
    self.current = token
    self.cursor += 1
}

parser_throw :: proc(self: ^Parser, format: string, args: ..any) -> ! {
    writer := self.writer
    col    := self.current.col
    fmt.wprintf(writer, format, ..args)
    fmt.wprintf(writer, " (line %i, col %i)", self.current.line, col)
    c.longjmp(&self.try_block.impl, 1)
}
