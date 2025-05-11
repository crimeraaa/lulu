#+private file
package lulu

import "core:strconv"
import "core:strings"
import "core:unicode"
import "core:unicode/utf8"

@(private="package")
Lexer :: struct {
    vm:             ^VM,      // Contains object list and string builder.
    input, source:   string,  // File data as text and its name.
    start, current:  int,     // Current lexeme iterators.
    line:            int,     // Current line number.
}

@(private="package")
Token :: struct {
    type:       Token_Type  `fmt:"s"`,
    lexeme:     string      `fmt:"q"`,
    line:       int         `fmt:"-"`,
    literal:    union {f64, ^OString},
}

@(private="package")
Token_Type :: enum {
    // Keywords
    And,            Break,          Do,
    Else,           Elseif,         End,
    False,          For,            Function,
    If,             In,             Local,
    Nil,            Not,            Or,
    Print, // Temporary!
    Repeat,         Return,         Then,
    True,           Until,          While,

    // Balanced Pairs
    Left_Paren,     Right_Paren,
    Left_Bracket,   Right_Bracket,
    Left_Curly,     Right_Curly,
    Left_Angle,     Right_Angle,
    Left_Angle_Eq,  Right_Angle_Eq,

    // Punctuation
    Equals,         Equals_2,       Tilde_Eq,
    Period,         Ellipsis_2,     Ellipsis_3,
    Comma,          Colon,          Semicolon,
    Pound,

    // Arithmetic
    Plus,           Dash,           Star,
    Slash,          Percent,        Caret,

    // Misc.
    Number,         String,         Identifier,
    Error,          Eof,
}

@(private="package", rodata)
token_type_strings := [Token_Type]string {
    .And            = "and",        .Break          = "break",      .Do         = "do",
    .Else           = "else",       .Elseif         = "elseif",     .End        = "end",
    .False          = "false",      .For            = "for",        .Function   = "function",
    .If             = "if",         .In             = "in",         .Local      = "local",
    .Nil            = "nil",        .Not            = "not",        .Or         = "or",
    .Print          = "print",
    .Repeat         = "repeat",     .Return         = "return",     .Then       = "then",
    .True           = "true",       .Until          = "until",      .While      = "while",

    .Left_Paren     = "(",          .Right_Paren    = ")",
    .Left_Bracket   = "[",          .Right_Bracket  = "]",
    .Left_Curly     = "{",          .Right_Curly    = "}",
    .Left_Angle     = "<",          .Right_Angle    = ">",
    .Left_Angle_Eq  = "<=",         .Right_Angle_Eq = ">=",

    .Equals         = "=",          .Equals_2       = "==",         .Tilde_Eq   = "~=",
    .Period         = ".",          .Ellipsis_2     = "..",         .Ellipsis_3 = "...",
    .Comma          = ",",          .Colon          = ":",          .Semicolon  = ";",
    .Pound          = "#",

    .Plus           = "+",          .Dash           = "-",          .Star       = "*",
    .Slash          = "/",          .Percent        = "%",          .Caret      = "^",
    .Number         = "<number>",   .String         = "<string>",   .Identifier = "<identifier>",
    .Error          = "<error>",    .Eof            = "<eof>",
}

@(private="package", require_results)
lexer_create :: proc(vm: ^VM, input: string, name: string) -> Lexer {
    return {
        vm      = vm,
        input   = input,
        source  = name,
        line    = 1,
    }
}

@(private="package", require_results)
lexer_scan_token :: proc(l: ^Lexer) -> Token {
    consume_whitespace(l)
    // This is necessary so that we point to the start of the actual lexeme.
    l.start = l.current
    if is_at_end(l^) {
        return create_token(l^, .Eof)
    }

    r := advance(l)
    switch {
    case is_alpha(r): return create_keyword_identifier_token(l)
    case is_digit(r): return create_number_token(l, r)
    }

    type := Token_Type.Error
    switch r {
    case '(': type = .Left_Paren
    case ')': type = .Right_Paren
    case '[': type = .Left_Bracket
    case ']': type = .Right_Bracket
    case '{': type = .Left_Curly
    case '}': type = .Right_Curly
    case '<': type = .Left_Angle_Eq  if matches(l, '=') else .Left_Angle
    case '>': type = .Right_Angle_Eq if matches(l, '=') else .Right_Angle
    case '~': type = .Tilde_Eq
        if !matches(l, '=') {
            lexer_error(l, "Expected '='")
        }
    case '=': type = .Equals_2 if matches(l, '=') else .Equals
    case '.':
        if matches(l, '.') {
            type = .Ellipsis_3 if matches(l, '.') else .Ellipsis_2
        } else {
            if rr := peek(l^); is_digit(rr) {
                return create_number_token(l, rr)
            }
            type = .Period
        }
    case ',': type = .Comma
    case ':': type = .Colon
    case ';': type = .Semicolon
    case '#': type = .Pound

    case '+': type = .Plus
    case '-': type = .Dash
    case '*': type = .Star
    case '/': type = .Slash
    case '%': type = .Percent
    case '^': type = .Caret
    case '\'', '\"': return create_string_token(l, r)
    }

    if type == .Error {
        lexer_error(l, "Unexpected symbol")
    }
    return create_token(l^, type)
}

@(require_results)
create_token :: proc(l: Lexer, type: Token_Type) -> Token {
    return Token{
        lexeme = l.input[l.start:l.current],
        line   = l.line,
        type   = type,
    }
}

@(require_results)
create_keyword_identifier_token :: proc(l: ^Lexer) -> Token {
    @(require_results)
    check_type :: proc(token: Token, type: Token_Type) -> Token_Type {
        assert(.And <= type && type <= .While)
        return type if token.lexeme == token_type_strings[type] else .Identifier
    }

    // Assumes all keywords are ASCII
    @(require_results)
    get_type :: proc(token: Token) -> Token_Type {
        switch token.lexeme[0] {
        case 'a': return check_type(token, .And)
        case 'b': return check_type(token, .Break)
        case 'd': return check_type(token, .Do)
        case 'e':
            switch len(token.lexeme) {
            case len("end"):    return check_type(token, .End)
            case len("else"):   return check_type(token, .Else)
            case len("elseif"): return check_type(token, .Elseif)
            }
        case 'f':
            switch len(token.lexeme) {
            case len("for"):        return check_type(token, .For)
            case len("false"):      return check_type(token, .False)
            case len("function"):   return check_type(token, .Function)
            }
        case 'i':
            if len(token.lexeme) != len("if") {
                break
            }
            switch token.lexeme[1] {
            case 'f': return check_type(token, .If)
            case 'n': return check_type(token, .In)
            }
        case 'l': return check_type(token, .Local)
        case 'n':
            if len(token.lexeme) != len("nil") {
                break
            }
            switch token.lexeme[1] {
            case 'i': return check_type(token, .Nil)
            case 'o': return check_type(token, .Not)
            }
        case 'o': return check_type(token, .Or)
        case 'p': return check_type(token, .Print)
        case 'r':
            if len(token.lexeme) != len("repeat") {
                break
            }
            switch token.lexeme[2] {
            case 'p': return check_type(token, .Repeat)
            case 't': return check_type(token, .Return)
            }
        case 't':
            if len(token.lexeme) != len("true") {
                break
            }
            switch token.lexeme[1] {
            case 'h': return check_type(token, .Then)
            case 'r': return check_type(token, .True)
            }
        case 'u': return check_type(token, .Until)
        case 'w': return check_type(token, .While)
        }

        return .Identifier
    }

    consume_sequence(l, is_alnum)
    token := create_token(l^, .Identifier)
    token.type = get_type(token)
    return token
}


@(require_results)
create_number_token :: proc(l: ^Lexer, prev: rune) -> Token {
    consume_number :: proc(l: ^Lexer, prev: rune = 0) -> (prefixed: bool) {
        /*
        Notes:
        -   0[bB] = binary
        -   0[oO] = octal
        -   0[xX] = hexadecimal
        -   Prefixed integers cannot contain a '.', so we do not recurse to consume it.
         */
        if prev == '0' && matches(l, "bBoOxX") {
            consume_sequence(l, is_alnum)
            return true
        }

        // Consume integer portion.
        consume_sequence(l, is_digit)

        // Have decimal?
        if matches(l, '.') {
            consume_sequence(l, is_digit)
            // Recursively parse in case of invalid, e.g: "1.2.3".
            consume_number(l)
        }

        // Have exponent form?
        if matches(l, "eE") {
            // WARNING(2025-01-25): Be careful not to introduce ambiguity!
            matches(l, "+-")
            consume_sequence(l, is_digit)
        }
        // Consume trailing characters so we can tell if this is a bad number.
        consume_sequence(l, is_alnum)
        return false
    }

    prefixed := consume_number(l, prev)
    token    := create_token(l^, .Number)

    ok: bool
    if prefixed {
        i: int
        // Do NOT shadow `ok`! We rely on it to check for errors.
        i, ok = strconv.parse_int(token.lexeme)
        token.literal = cast(f64)i
    } else {
        token.literal, ok = strconv.parse_f64(token.lexeme)
    }
    if !ok {
        lexer_error(l, "Malformed number", token.lexeme)
    }
    return token
}

@(require_results)
create_string_token :: proc(l: ^Lexer, quote: rune) -> Token {
    @(require_results)
    check_rune :: proc(l: ^Lexer, r: rune) -> rune {
        r := r
        if r != '\\' {
            return r
        }
        // Have an escape character, so skip slash and read escape sequence
        r = advance(l)
        switch r {
        case 'a': return '\a'
        case 'b': return '\b'
        case 'f': return '\f'
        case 'n': return '\n'
        case 'r': return '\r'
        case 't': return '\t'
        case 'v': return '\v'
        case '\\', '\'', '\"': return r // `r` is already correct as-is
        }

        // `r` may not necessarily be an ASCII character
        buf: [2 * size_of(rune)]byte
        tmp := strings.builder_from_bytes(buf[:])
        strings.write_rune(&tmp, '\\')
        strings.write_rune(&tmp, r)
        lexer_error(l, "Invalid escape sequence", strings.to_string(tmp))
    }

    builder := vm_get_builder(l.vm)
    for !is_at_end(l^) && peek(l^) != quote {
        r := advance(l)
        if r == '\n' {
            break
        }
        strings.write_rune(builder, check_rune(l, r))
    }
    if is_at_end(l^) || !matches(l, quote) {
        lexer_error(l, "Unterminated string")
    }
    token := create_token(l^, .String)
    token.literal = ostring_new(l.vm, strings.to_string(builder^))
    return token
}

is_alpha :: proc(r: rune) -> bool {
    return r == '_' || unicode.is_alpha(r)
}

is_digit :: unicode.is_digit

is_alnum :: proc(r: rune) -> bool {
    return is_digit(r) || is_alpha(r)
}

consume_sequence :: proc(l: ^Lexer, $callback: proc(r: rune) -> bool) {
    // The `is_at_end` check is VERY important in case we hit <EOF> here!
    for !is_at_end(l^) && callback(peek(l^)) {
        // WARNING(2025-01-18): May throw!
        advance(l)
    }
}


consume_whitespace :: proc(l: ^Lexer) {
    @(require_results)
    check_multiline :: proc(l: ^Lexer) -> (opening: int, is_multiline: bool) {
        if !matches(l, '[') {
            return 0, false
        }
        opening      = count_nesting(l)
        is_multiline = matches(l, '[')
        return opening, is_multiline
    }

    skip_comment :: proc(l: ^Lexer) {
        // Skip the 2 '-'.
        advance(l)
        advance(l)
        if opening, is_multiline := check_multiline(l); is_multiline {
            consume_multiline(l, opening, is_comment = true)
        } else {
            /*
            Notes(2025-04-15):
            -   Sometimes get an LLVM linkage error if we use a named but scoped
                function. Perhaps a race condition in the Odin compiler?
             */
            consume_sequence(l, proc(r: rune) -> bool {
                return r != '\n'
            })
        }
    }

    for {
        switch peek(l^) {
        case '\n':
            l.line += 1
            fallthrough
        case ' ', '\r', '\t':
            advance(l)
        case '-':
            if peek_next(l^) != '-' {
                return
            }
            skip_comment(l)
        case:
            return
        }
    }
}

consume_multiline :: proc(l: ^Lexer, opening: int, $is_comment: bool) {
    defer if is_comment {
        l.start = l.current
    }

    consume_loop: for {
        if matches(l, ']') {
            closing := count_nesting(l)
            if closing == opening && matches(l, ']') {
                break consume_loop
            }
        }

        if is_at_end(l^) {
            lexer_error(l, "Unterminated multiline sequence")
        }

        if advance(l) == '\n' {
            l.line += 1
        }
    }
}

lexer_error :: proc(l: ^Lexer, msg: string, lexeme := "") -> ! {
    lexeme := l.input[l.start:l.current] if lexeme == "" else lexeme
    vm_compile_error(l.vm, l.source, l.line, "%s at '%s'", msg, lexeme)
}

@(require_results)
count_nesting :: proc(l: ^Lexer) -> (count: int) {
    for matches(l, '=') {
        count += 1
    }
    return count
}

/*
Notes:
    - Returns the CURRENT character but also advances the iterator index.
*/
advance :: proc(l: ^Lexer) -> rune {
    r, size := utf8.decode_rune(l.input[l.current:])
    l.current += size
    if r == utf8.RUNE_ERROR {
        lexer_error(l, "Invalid UTF-8 sequence")
    }
    return r
}

matches :: proc {
    match_rune,
    match_any,
}

match_rune :: proc(l: ^Lexer, want: rune) -> (found: bool) {
    if found = peek(l^) == want; found {
        advance(l)
    }
    return found
}

match_any :: proc(l: ^Lexer, want: string) -> (found: bool) {
    if found = strings.index_rune(want, peek(l^)) != -1; found {
        advance(l)
    }
    return found
}

peek :: proc(l: Lexer, offset := 0) -> (r: rune) {
    return utf8.rune_at(l.input, l.current + offset)
}

peek_next :: proc(l: Lexer) -> (r: rune) {
    if is_at_end(l) {
        return utf8.RUNE_ERROR
    }
    return peek(l, 1)
}

is_at_end :: proc(l: Lexer) -> bool {
    return l.current >= len(l.input)
}
