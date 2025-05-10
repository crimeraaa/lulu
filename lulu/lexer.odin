#+private
package lulu

import "core:strconv"
import "core:strings"
import "core:text/match"
import "core:unicode"
import "core:unicode/utf8"

Lexer :: struct {
    vm:             ^VM,      // Contains object list and string builder.
    input, source:   string,  // File data as text and its name.
    start, current:  int,     // Current lexeme iterators.
    line:            int,     // Current line number.
}

Token :: struct {
    type:       Token_Type  `fmt:"s"`,
    lexeme:     string,
    line:       int         `fmt:"-"`,
    literal:    union {f64, ^OString},
}

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

@(rodata)
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

@(require_results)
lexer_create :: proc(vm: ^VM, input: string, name: string) -> (lexer: Lexer) {
    lexer.vm      = vm
    lexer.input   = input
    lexer.source  = name
    lexer.line    = 1
    return lexer
}

@(require_results)
lexer_scan_token :: proc(lexer: ^Lexer) -> (token: Token) {
    consume_whitespace(lexer)
    // This is necessary so that we point to the start of the actual lexeme.
    lexer.start = lexer.current
    if is_at_end(lexer^) {
        return create_token(lexer, .Eof)
    }

    r := advance(lexer)
    switch {
    case is_alpha(r):       return create_keyword_identifier_token(lexer)
    case match.is_digit(r): return create_number_token(lexer, r)
    }

    type := Token_Type.Error
    switch r {
    case '(': type = .Left_Paren
    case ')': type = .Right_Paren
    case '[': type = .Left_Bracket
    case ']': type = .Right_Bracket
    case '{': type = .Left_Curly
    case '}': type = .Right_Curly
    case '<': type = .Left_Angle_Eq  if matches(lexer, '=') else .Left_Angle
    case '>': type = .Right_Angle_Eq if matches(lexer, '=') else .Right_Angle
    case '~': type = .Tilde_Eq
        if !matches(lexer, '=') {
            lexer_error(lexer, "Expected '='")
        }
    case '=': type = .Equals_2 if matches(lexer, '=') else .Equals
    case '.':
        if matches(lexer, '.') {
            type = .Ellipsis_3 if matches(lexer, '.') else .Ellipsis_2
        } else {
            if rr := peek(lexer^); match.is_digit(rr) {
                return create_number_token(lexer, rr)
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
    case '\'', '\"': return create_string_token(lexer, r)
    }

    if type == .Error {
        lexer_error(lexer, "Unexpected symbol")
    }
    return create_token(lexer, type)
}

@(private="file", require_results)
create_token :: proc(lexer: ^Lexer, type: Token_Type) -> (token: Token) {
    token.lexeme = lexer.input[lexer.start:lexer.current]
    token.line   = lexer.line
    token.type   = type
    lexer.start  = lexer.current
    return token
}

@(private="file", require_results)
create_keyword_identifier_token :: proc(lexer: ^Lexer) -> (token: Token) {
    consume_sequence(lexer, is_alnum)
    token = create_token(lexer, .Identifier)

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

    token.type = get_type(token)
    return token
}


@(private="file", require_results)
create_number_token :: proc(lexer: ^Lexer, prev: rune) -> (token: Token) {
    consume_number :: proc(lexer: ^Lexer, prev: rune = 0) -> (is_prefixed: bool) {
        /*
        Notes:
        -   0[bB] = binary
        -   0[oO] = octal
        -   0[xX] = hexadecimal
        -   Prefixed integers cannot contain a '.', so we do not recurse to consume it.
         */
        if prev == '0' && matches(lexer, "bBoOxX") {
            consume_sequence(lexer, is_alnum)
            return true
        }

        // Consume integer portion.
        consume_sequence(lexer, match.is_digit)

        // Have decimal?
        if matches(lexer, '.') {
            consume_sequence(lexer, match.is_digit)
            // Recursively parse in case of invalid, e.g: "1.2.3".
            consume_number(lexer)
        }

        // Have exponent form?
        if matches(lexer, "eE") {
            // WARNING(2025-01-25): Be careful not to introduce ambiguity!
            matches(lexer, "+-")
            consume_sequence(lexer, match.is_digit)
        }
        // Consume trailing characters so we can tell if this is a bad number.
        consume_sequence(lexer, is_alnum)
        return false
    }

    is_prefixed := consume_number(lexer, prev)
    token = create_token(lexer, .Number)
    ok: bool
    if is_prefixed {
        i: int
        // Do NOT shadow `ok`! We rely on it to check for errors.
        i, ok = strconv.parse_int(token.lexeme)
        token.literal = cast(f64)i
    } else {
        token.literal, ok = strconv.parse_f64(token.lexeme)
    }
    if !ok {
        lexer_error(lexer, "Malformed number", token.lexeme)
    }
    return token
}

@(private="file", require_results)
create_string_token :: proc(lexer: ^Lexer, quote: rune) -> (token: Token) {
    builder := vm_get_builder(lexer.vm)
    for !is_at_end(lexer^) && peek(lexer^) != quote {
        r := advance(lexer)
        if r == '\n' {
            break
        }
        // If have escape character, skip it and read the escaped sequence
        if r == '\\' {
            switch r = advance(lexer); r {
            case 'a': r = '\a'
            case 'b': r = '\b'
            case 'f': r = '\f'
            case 'n': r = '\n'
            case 'r': r = '\r'
            case 't': r = '\t'
            case 'v': r = '\v'
            case '\\', '\'', '\"': // `r` is already the correct escape char
                break
            case:
                // `r` may not necessarily be an ASCII character
                buf: [2 * size_of(rune)]byte
                tmp := strings.builder_from_bytes(buf[:])
                strings.write_rune(&tmp, '\\')
                strings.write_rune(&tmp, r)
                lexer_error(lexer, "Invalid escape sequence", strings.to_string(tmp))
            }
        }
        strings.write_rune(builder, r)
    }
    if is_at_end(lexer^) || !matches(lexer, quote) {
        lexer_error(lexer, "Unterminated string")
    }
    token = create_token(lexer, .String)
    token.literal = ostring_new(lexer.vm, strings.to_string(builder^))
    return token
}

@(private="file")
is_alpha :: proc(r: rune) -> bool {
    return r == '_' || unicode.is_alpha(r)
}

@(private="file")
is_alnum :: proc(r: rune) -> bool {
    return match.is_digit(r) || is_alpha(r)
}

@(private="file")
consume_sequence :: proc(lexer: ^Lexer, $callback: proc(r: rune) -> bool) {
    // The `is_at_end` check is VERY important in case we hit <EOF> here!
    for !is_at_end(lexer^) && callback(peek(lexer^)) {
        // WARNING(2025-01-18): May throw!
        advance(lexer)
    }
}


@(private="file")
consume_whitespace :: proc(lexer: ^Lexer) {
    @(require_results)
    check_multiline :: proc(lexer: ^Lexer) -> (opening: int, is_multiline: bool) {
        if !matches(lexer, '[') {
            return 0, false
        }
        opening      = count_nesting(lexer)
        is_multiline = matches(lexer, '[')
        return opening, is_multiline
    }

    skip_comment :: proc(lexer: ^Lexer) {
        // Skip the 2 '-'.
        advance(lexer)
        advance(lexer)
        if opening, is_multiline := check_multiline(lexer); is_multiline {
            consume_multiline(lexer, opening, is_comment = true)
        } else {
            /*
            Notes(2025-04-15):
            -   Sometimes get an LLVM linkage error if we use a named but scoped
                function. Perhaps a race condition in the Odin compiler?
             */
            consume_sequence(lexer, proc(r: rune) -> bool {
                return r != '\n'
            })
        }
    }

    for {
        switch peek(lexer^) {
        case '\n':
            lexer.line += 1
            fallthrough
        case ' ', '\r', '\t':
            advance(lexer)
        case '-':
            if peek_next(lexer^) != '-' {
                return
            }
            skip_comment(lexer)
        case:
            return
        }
    }
}

@(private="file")
consume_multiline :: proc(lexer: ^Lexer, opening: int, $is_comment: bool) {
    defer if is_comment {
        lexer.start = lexer.current
    }

    consume_loop: for {
        if matches(lexer, ']') {
            closing := count_nesting(lexer)
            if closing == opening && matches(lexer, ']') {
                break consume_loop
            }
        }

        if is_at_end(lexer^) {
            lexer_error(lexer, "Unterminated multiline sequence")
        }

        if advance(lexer) == '\n' {
            lexer.line += 1
        }
    }
}

lexer_error :: proc(lexer: ^Lexer, msg: string, lexeme := "") -> ! {
    source, line := lexer.source, lexer.line
    lexeme := lexeme
    if lexeme == "" {
        lexeme = lexer.input[lexer.start:lexer.current]
    }
    vm_compile_error(lexer.vm, source, line, "%s at '%s'", msg, lexeme)
}

@(private="file", require_results)
count_nesting :: proc(lexer: ^Lexer) -> (count: int) {
    for matches(lexer, '=') {
        count += 1
    }
    return count
}

/*
Notes:
    - Returns the CURRENT character but also advances the iterator index.
*/
@(private="file")
advance :: proc(lexer: ^Lexer) -> rune {
    rr, size := utf8.decode_rune(lexer.input[lexer.current:])
    lexer.current += size
    if rr == utf8.RUNE_ERROR {
        lexer_error(lexer, "Invalid UTF-8 sequence")
    }
    return rr
}

@(private="file")
matches :: proc {
    match_rune,
    match_any,
}

@(private="file")
match_rune :: proc(lexer: ^Lexer, want: rune) -> (found: bool) {
    if peek(lexer^) == want {
        advance(lexer)
        return true
    }
    return false
}

@(private="file")
match_any :: proc(lexer: ^Lexer, want: string) -> (found: bool) {
    if strings.index_rune(want, peek(lexer^)) != -1 {
        advance(lexer)
        return true
    }
    return false
}

@(private="file")
peek :: proc(lexer: Lexer) -> (r: rune) {
    return utf8.rune_at(lexer.input, lexer.current)
}

@(private="file")
peek_next :: proc(lexer: Lexer) -> (r: rune) {
    if is_at_end(lexer) {
        return utf8.RUNE_ERROR
    }
    return utf8.rune_at(lexer.input, lexer.current + 1)
}

@(private="file")
is_at_end :: proc(lexer: Lexer) -> bool {
    return lexer.current >= len(lexer.input)
}
