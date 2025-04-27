package cdecl

import "core:strings"
import "core:unicode/utf8"
import "core:unicode"

Lexer :: struct {
    input:         string   `fmt:"q"`,
    start, cursor: int, // Absolute indexes into `input`>
    line:          int,
    line_index:    int, // Absolute index of the start of current line in `input`.
}

Token :: struct {
    type:      Token_Type  `fmt:"s"`,
    lexeme:    string      `fmt:"q"`,
    line, col: int,
}

Token_Type :: enum u8 {
    Invalid,

    // Keywords (that we care about)
    Void, Bool, Char, Short, Int, Long,
    Signed, Unsigned,
    Float, Double, Complex,
    Struct, Enum, Union,
    Const, Volatile,
    Restrict, Register,

    // Encloses Pointer-to-array or Pointer-to-function
    LParen, LBracket,

    // Array literals
    RParen, RBracket,

    // Pointer, Terminals
    Pointer, Ident, Integer_Literal, Eof,
}

token_to_string :: proc(type: Token_Type) -> string {
    @(static, rodata)
    token_strings := [Token_Type]string{
        .Invalid  = "<invalid>",

        // Keywords (that we care about)
        .Void     = "void",     .Bool     = "bool",
        .Char     = "char",     .Short    = "short",    .Int     = "int",     .Long = "long",
        .Signed   = "signed",   .Unsigned = "unsigned",
        .Float    = "float",    .Double   = "double",   .Complex = "complex",
        .Struct   = "struct",   .Enum     = "enum",     .Union   = "union",
        .Const    = "const",    .Volatile = "volatile",
        .Register = "register", .Restrict = "restrict",


        // Encloses Pointer-to-array or Pointer-to-function
        .LParen   = "(", .RParen   = ")",

        // Array literals
        .LBracket = "[", .RBracket = "]",

        // Pointer, Terminals
        .Pointer = "*", .Ident = "<ident>", .Integer_Literal = "<integer>", .Eof = "<eof>",
    }
    return token_strings[type]
}

lexer_make :: proc(input: string) -> Lexer {
    return Lexer{input = input, line = 1}
}

lexer_lex :: proc(self: ^Lexer) -> Token {
    @require_results
    get_type :: proc(r: rune) -> Token_Type {
        switch r {
        case '(': return .LParen
        case ')': return .RParen
        case '[': return .LBracket
        case ']': return .RBracket
        case '*': return .Pointer
        case:     return .Invalid
        }
    }

    lexer_skip_whitespace(self)
    if lexer_eof(self^) {
        return lexer_make_token(self^, .Eof)
    }

    // Start of lexeme is the first non-whitespace
    self.start = self.cursor
    r := lexer_consume(self)
    if unicode.is_alpha(r) {
        // Keyword or identifier?
        // Advance returned the previous, so peek the current
        r = lexer_peek(self^)
        for unicode.is_alpha(r) || r == '_' {
            r = lexer_next(self)
        }
        return lexer_keyword(self)
    } else if unicode.is_digit(r) {
        // Integer literal?
        r = lexer_peek(self^)
        for unicode.is_digit(r) {
            r = lexer_next(self)
        }
        return lexer_make_token(self^, .Integer_Literal)
    }
    return lexer_make_token(self^, get_type(r))
}

lexer_make_token :: proc(self: Lexer, type: Token_Type, word := "") -> Token {
    word := word if word != "" else self.input[self.start:self.cursor]
    col  := lexer_col(self)
    return Token{type = type, lexeme = word, line = self.line, col = col}
}

lexer_line :: proc(self: Lexer) -> string {
    return strings.trim_left(self.input[self.line_index:], "\n")
}

lexer_col :: proc(self: Lexer) -> int {
    // Add 1 because would otherwise by 0-based column into this line
    return (self.start - self.line_index) + 1
}

lexer_skip_whitespace :: proc(self: ^Lexer) {
    for {
        r, ok := lexer_peek(self^)
        switch r {
        case '\n':
            self.line += 1
            // Add 1 because we want to point to the first non-newline
            self.line_index = self.cursor + 1
            fallthrough
        case ' ', '\t', '\v', '\r':
            lexer_consume(self)
            continue
        case:
            return
        }
    }
}

/*
**Overview**
-   Return the current rune and update the cursor.
*/
lexer_consume :: proc(self: ^Lexer) -> (read: rune) {
    decoded, size := utf8.decode_rune(self.input[self.cursor:])
    self.cursor += size
    return decoded
}

lexer_peek :: proc(self: Lexer) -> (read: rune, ok: bool) #optional_ok {
    if lexer_eof(self) {
        return utf8.RUNE_ERROR, false
    }

    // Decodes the first rune starting at index 0, so we need a slice.
    decoded, _ := utf8.decode_rune(self.input[self.cursor:])
    return decoded, true
}

/*
**Overview**
-   Update the cursor the return the rune being pointed at by the new cursor.
*/
lexer_next :: proc(self: ^Lexer) -> (prev: rune) {
    lexer_consume(self)
    return lexer_peek(self^)
}

lexer_eof :: proc(self: Lexer) -> bool {
    // We exhausted the input OR we overflowed?
    return self.cursor >= len(self.input) || self.cursor < 0
}

lexer_keyword :: proc(self: ^Lexer) -> Token {
    @require_results
    check_keyword :: proc(type: Token_Type, word: string) -> Token_Type {
        return type if word == to_string(type) else .Ident
    }

    word := self.input[self.start:self.cursor]
    type := Token_Type.Ident
    switch word[0] {
    case 'c':
        switch len(word) {
        case len("char"):    type = check_keyword(.Char, word)
        case len("const"):   type = check_keyword(.Const, word)
        case len("complex"): type = check_keyword(.Complex, word)
        }
    case 'd': type = check_keyword(.Double, word)
    case 'f': type = check_keyword(.Float, word)
    case 'i': type = check_keyword(.Int, word)
    case 'l': type = check_keyword(.Long, word)
    case 's':
        // len("struct") == len("signed")
        if len(word) != len("struct") {
            break
        }
        switch word[1] {
        case 'i': type = check_keyword(.Signed, word)
        case 't': type = check_keyword(.Struct, word)
        }
    case 'u':
        switch len(word) {
        case len("union"):    type = check_keyword(.Union, word)
        case len("unsigned"): type = check_keyword(.Unsigned, word)
        }
    case 'v':
        switch len(word) {
        case len("void"):     type = check_keyword(.Void, word)
        case len("volatile"): type = check_keyword(.Volatile, word)
        }
    }
    return lexer_make_token(self^, type, word)
}
