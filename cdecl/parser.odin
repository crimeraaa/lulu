package cdecl

import c "core:c/libc"
import "core:fmt"
import "core:io"

Parser :: struct {
    lexer:              Lexer,
    current, lookahead: Token,
    primary:            Declaration,
    writer:             io.Writer,
    error_handler:      c.jmp_buf    `fmt:"-"`,
}

parser_make :: proc(input: string, writer: io.Writer) -> Parser {
    return Parser{
        lexer   = lexer_make(input),
        primary = declaration_make(),
        writer  = writer,
    }
}

parser_parse :: proc(self: ^Parser) -> bool {
    if c.setjmp(&self.error_handler) == 0 {
        parser_advance(self)
        parser_decl(self, &self.primary)
        return true
    }
    return false
}

parser_prefix :: proc(self: ^Parser, decl: ^Declaration) {

    set_prefix :: proc(self: ^Parser, decl: ^Declaration, prefix: Prefix) {
        if decl.prefix == .None {
            decl.prefix = prefix
            parser_consume(self, .Ident)
            return
        }
        parser_throw(self, "Cannot combine prefixes %q and %q",
                     to_string(decl.prefix), self.current.lexeme)
    }

    #partial switch self.current.type {
    case .Struct: set_prefix(self, decl, .Struct)
    case .Enum:   set_prefix(self, decl, .Enum)
    case .Union:  set_prefix(self, decl, .Union)
    }
}

parser_decl :: proc(self: ^Parser, decl: ^Declaration) {
    for self.current.type != .Eof {
        parser_prefix(self, decl)
        parser_basic(self, decl)
    }
    parser_canonicalize(self, decl)
}

parser_basic :: proc(self: ^Parser, decl: ^Declaration) {

    set_tag :: proc(self: ^Parser, decl: ^Declaration, tag: Tag) {
        if decl.tag == .None {
            decl.tag = tag
            parser_advance(self)
            return
        }
        have_tag(self, decl, tag)
    }

    have_tag :: proc(self: ^Parser, decl: ^Declaration, got: Tag) -> ! {
        parser_throw(self, "Cannot combine tags %q ann %q",
                     to_string(decl.tag), to_string(got))
    }

    set_modifier :: proc(self: ^Parser, decl: ^Declaration, mod: Modifier) {
        if decl.modifier == .None {
            decl.modifier = mod
            parser_advance(self)
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
            parser_advance(self)
            return
        }
        parser_throw(self, "Duplicate qualifier %q", to_string(qual))
    }

    // The following is an absolute monstrosity
    #partial switch self.current.type {
    case .Void: set_tag(self, decl, .Void)
    case .Bool: set_tag(self, decl, .Bool)
    case .Char: set_tag(self, decl, .Char)

    // Integers
    case .Short:
        #partial switch decl.tag {
        case .None: fallthrough // `short`
        case .Int:  decl.tag = .Short // `int short`
        case: have_tag(self, decl, .Short)
        }
        parser_advance(self)
    case .Int:
        #partial switch decl.tag {
        case .None:      decl.tag = .Int       // `int`
        case .Short:     decl.tag = .Short     // `short int`
        case .Long:      decl.tag = .Long      // `long int`
        case .Long_Long: decl.tag = .Long_Long // `long long int`
        case: have_tag(self, decl, .Int)
        }
        parser_advance(self)
    case .Long:
        #partial switch decl.tag {
        case .None:   fallthrough             // `long`
        case .Int:    decl.tag = .Long        // `int long`
        case .Long:   decl.tag = .Long_Long   // `long long`, `int long long`
        case .Double: decl.tag = .Long_Double // `double long`
        case: have_tag(self, decl, .Long)
        }
        parser_advance(self)

    // Floating-point
    case .Float:
        set_tag(self, decl, .Float)
    case .Double:
        #partial switch decl.tag {
        case .None: decl.tag = .Double      // `double`
        case .Long: decl.tag = .Long_Double // `long double`
        case: have_tag(self, decl, .Double)
        }
        parser_advance(self)

    // Modifiers
    case .Signed:   set_modifier(self, decl, .Signed)
    case .Unsigned: set_modifier(self, decl, .Unsigned)
    case .Complex:  set_modifier(self, decl, .Complex)

    // Qualifiers
    case .Const:    set_qualifier(self, decl, .Const)
    case .Volatile: set_qualifier(self, decl, .Volatile)

    case:
        parser_throw(self, "Unexpected token: %q", self.current.lexeme)
    }
}

parser_canonicalize :: proc(self: ^Parser, decl: ^Declaration) {
    bad_modifier :: proc(self: ^Parser, decl: ^Declaration) -> ! {
        parser_throw(self, "Cannot use %q on %q", to_string(decl.modifier),
                     to_string(decl.tag))
    }

    // Need to deduce default types for lone modifiers?
    if decl.tag == .None {
        switch decl.modifier {
        case .None: parser_throw(self, "Got no tag nor modifier")
        case .Signed:   fallthrough         // `signed` == `int`
        case .Unsigned: decl.tag = .Int     // `unsigned` == `unsigned int`
        case .Complex:  decl.tag = .Double  // `complex` == `double complex`
        }
    }

    // Verify that all modifiers are applie to their appropriate types.
    #partial switch decl.modifier {
    case .None:
        // `char` is not guaranteed to be signed nor unsigned.
        if .Short <= decl.tag && decl.tag <= .Long_Long {
            decl.modifier = .Signed
        }
    case .Signed, .Unsigned:
        if tag_is_integer(decl.tag) {
            break
        }
        bad_modifier(self, decl)
    case .Complex:
        if tag_is_floating(decl.tag) {
            break
        }
        // `long complex` or `complex long`
        if decl.tag == .Long {
            decl.tag = .Long_Double
            break
        }
        bad_modifier(self, decl)
    }
}

parser_fini :: proc(self: ^Parser) {
    decl   := self.primary
    writer := self.writer
    for qual in decl.qualifiers {
        fmt.wprintf(writer, "%s ", to_string(qual))
    }

    switch decl.modifier {
    case .None, .Complex:
        break
    case .Signed:
        // `signed int` and such are redundant; only `signed` char matters
        if decl.tag != .Char {
            break
        }
        fallthrough
    case .Unsigned:
        fmt.wprintf(writer, "%s ", to_string(decl.modifier))
    }

    // `float complex`, `double complex` are standard-ish canonical names
    // See: https://learn.microsoft.com/en-us/cpp/c-runtime-library/complex-math-support?view=msvc-170
    if decl.modifier == .Complex {
        fmt.wprintf(writer, "%s %s", to_string(decl.tag), to_string(decl.modifier))
    } else {
        fmt.wprint(writer, to_string(decl.tag))
    }
}

parser_advance :: proc(self: ^Parser) {
    self.current = lexer_lex(&self.lexer)
}

parser_consume :: proc(self: ^Parser, expected: Token_Type) {
    token := lexer_lex(&self.lexer)
    if token.type != expected {
        parser_throw(self, "Expected %q, got %q", to_string(expected),
                     token.lexeme)
    }
    self.current = token
}

parser_throw :: proc(self: ^Parser, format: string, args: ..any) -> ! {
    writer := self.writer
    col    := self.current.col
    fmt.wprintf(writer, format, ..args)
    fmt.wprintfln(writer, " (line %i, col %i)", self.current.line, col)
    fmt.wprintln(writer, lexer_line(self.lexer))
    fmt.wprintf(writer, "%*s^", col - 1, "")
    c.longjmp(&self.error_handler, 1)
}
