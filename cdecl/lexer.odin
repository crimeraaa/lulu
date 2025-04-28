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
    // Zero-value (default type)
    Invalid,

    // Keywords (that we care about)
    Void, Bool, Char, Short, Int, Long,
    Float, Double,
    Struct, Enum, Union,
    
    // Modifiers
    Signed, Unsigned, Complex,
    
    // CV Qualifiers
    Const, Volatile,

    // Encloses Pointer-to-array or Pointer-to-function
    LParen, LBracket,

    // Array literals
    RParen, RBracket,

    // Pointer
    Asterisk,

    // Terminals
    Semicolon, Ident, Integer_Literal, Eof,
}

token_to_string :: proc(type: Token_Type) -> string {
    // So many lookup tables!
    @(static, rodata)
    token_strings := [Token_Type]string{
        .Invalid  = "<invalid>",

        // Keywords (that we care about)
        .Void     = "void",     .Bool     = "bool",
        .Char     = "char",     .Short    = "short",    .Int     = "int",     .Long = "long",
        .Float    = "float",    .Double   = "double",
        .Struct   = "struct",   .Enum     = "enum",     .Union   = "union",
        .Signed   = "signed",   .Unsigned = "unsigned", .Complex = "complex",
        .Const    = "const",    .Volatile = "volatile",

        // Encloses Pointer-to-array or Pointer-to-function
        .LParen   = "(", .RParen   = ")",

        // Array literals
        .LBracket = "[", .RBracket = "]",

        // Pointer
        .Asterisk = "*",
        
        // Terminals
        .Semicolon = ";",
        .Ident     = "<ident>", .Integer_Literal = "<integer>", .Eof = "<eof>",
    }
    return token_strings[type]
}

tokenize :: proc(line: string) -> []Token {
    lexer  := lexer_make(line)
    tokens := make([dynamic]Token)
    for {
        token := lexer_lex(&lexer)
        append(&tokens, token)
        if token.type == .Invalid || token.type == .Eof {
            break
        }
    }
    return tokens[:]
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
        case '*': return .Asterisk
        case ';': return .Semicolon
        case:     return .Invalid
        }
    }

    lexer_skip_whitespace(self)
    if lexer_eof(self^) {
        return lexer_make_token(self^, .Eof)
    }

    // Start of lexeme is the first non-whitespace
    self.start = self.cursor
    switch r := lexer_consume(self); {
    case unicode.is_alpha(r):
        // Keyword or identifier?
        // Advance returned the previous, so peek the current
        r = lexer_peek(self^)
        for unicode.is_alpha(r) || r == '_' {
            r = lexer_next(self)
        }
        return lexer_keyword(self)
    case unicode.is_digit(r):
        // Integer literal?
        r = lexer_peek(self^)
        for unicode.is_digit(r) {
            r = lexer_next(self)
        }
        return lexer_make_token(self^, .Integer_Literal)
    case:
        return lexer_make_token(self^, get_type(r))
    }
}

lexer_make_token :: proc(self: Lexer, type: Token_Type, word := "") -> Token {
    word := word if word != "" else self.input[self.start:self.cursor]
    col  := lexer_col(self) + 1
    return Token{type = type, lexeme = word, line = self.line, col = col}
}

/*
**Overview**
-   Returns a string view into the input containing the current line.
-   This is mainly useful for error reporting.
*/
lexer_line :: proc(self: Lexer) -> string {
    return strings.trim_left(self.input[self.line_index:], "\n")
}


/*
**Overview**
-   Returns the 0-based index into the current line.
 */
lexer_col :: proc(self: Lexer) -> int {
    return (self.start - self.line_index)
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
    /*
    **Notes** (2025-04-28)
    -   Theoretically, linear search is really slow.
    -   In practice however our worst case is 3 elements.
    -   It's not like the lexer is the bottleneck for performance anyway.
    */
    @require_results
    check_keywords :: proc(word: string, types: ..Token_Type) -> Token_Type {
        for type in types {
            if word == to_string(type) {
                return type
            }
        }
        return .Ident
    }

    word := self.input[self.start:self.cursor]
    type := Token_Type.Ident
    switch word[0] {
    case 'c': type = check_keywords(word, .Char, .Const, .Complex)
    case 'd': type = check_keywords(word, .Double)
    case 'e': type = check_keywords(word, .Enum)
    case 'f': type = check_keywords(word, .Float)
    case 'i': type = check_keywords(word, .Int)
    case 'l': type = check_keywords(word, .Long)
    case 's': type = check_keywords(word, .Short, .Signed, .Struct)
    case 'u': type = check_keywords(word, .Union, .Unsigned)
    case 'v': type = check_keywords(word, .Void, .Volatile)
    }
    return lexer_make_token(self^, type, word)
}
