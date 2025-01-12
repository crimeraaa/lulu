#+private
package lulu

import "core:strconv"
import "core:strings"
import "core:text/match"
import "core:unicode"
import "core:unicode/utf8"

Lexer :: struct {
    vm              : ^VM,      // Contains object list and string builder.
    input, source   :  string,  // File data as text and its name.
    number          :  f64,     // Number literal, if we had any.
    str             : ^OString, // String literal, if we had any.
    start, current  :  int,     // Current lexeme iterators.
    line            :  int,     // Current line number.
}

Lexer_Error :: enum {
    None,
    Bad_Rune,               // Some utf8.* proc returned RUNE_ERROR?
    Unexpected_Character,
    Malformed_Number,       // Unable to convert lexeme to f64?
    Missing_Equals,         // For ~= specifically as it's the only token like this.
    Unterminated_String,
    Unterminated_Multiline,
    Eof = -1,               // Expected/normal
}

@(rodata)
lexer_error_strings := [Lexer_Error]string {
    .None                   = "none",
    .Bad_Rune               = "bad rune",
    .Unexpected_Character   = "unexpected character",
    .Malformed_Number       = "malformed number",
    .Missing_Equals         = "expected '='",
    .Unterminated_String    = "unterminated string",
    .Unterminated_Multiline = "unterminated multiline comment/string",
    .Eof                    = "<eof>"
}

Token :: struct {
    lexeme  : string,
    line    : int         `fmt:"-"`,
    type    : Token_Type  `fmt:"s"`,
}

Token_Type :: enum u8 {
    // Keywords
    And,            Break,          Do,
    Else,           Elseif,         End,
    False,          For,            Function,
    If,             In,             Local,
    Nil,            Not,            Or,
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
lexer_scan_token :: proc(lexer: ^Lexer) -> (token: Token, error: Lexer_Error) {
    if error = consume_whitespace(lexer); error != nil {
        return create_token(lexer, .Error), error
    }
    if is_at_end(lexer^) {
        return create_token(lexer, .Eof), .Eof
    }

    r, err := advance(lexer)
    if err != nil {
        return create_token(lexer, .Error), err
    }

    switch {
    case is_alpha(r):       return create_keyword_identifier_token(lexer)
    case match.is_digit(r): return create_number_token(lexer, r)
    }

    type := Token_Type.Error
    error = nil
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
        // Assign 'error' to differentiate from .Unexpected_Character
        if !matches(lexer, '=') {
            error = .Missing_Equals
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
    return create_token(lexer, type), .Unexpected_Character if type == .Error else nil
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
create_keyword_identifier_token :: proc(lexer: ^Lexer) -> (token: Token, error: Lexer_Error) {
    error = consume_sequence(lexer, is_alnum)
    token = create_token(lexer, .Identifier)

    if error != nil {
        return token, error
    }

    @(require_results)
    check_type :: proc(token: Token, $type: Token_Type) -> Token_Type where type <= .While {
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
    return token, error
}


@(private="file", require_results)
create_number_token :: proc(lexer: ^Lexer, prev: rune) -> (token: Token, error: Lexer_Error) {
    @(require_results)
    consume_number :: proc(lexer: ^Lexer, prev: rune = 0) -> (error: Lexer_Error) {
        if prev == '0' && matches(lexer, "xX") {
            consume_sequence(lexer, match.is_xdigit) or_return
            consume_sequence(lexer, is_alnum)        or_return
            return nil
        }
        // Consume integer portion.
        consume_sequence(lexer, match.is_digit) or_return

        // Have decimal?
        if matches(lexer, '.') {
            consume_sequence(lexer, match.is_digit) or_return
            // Recursively parse in case of invalid, e.g: "1.2.3".
            consume_number(lexer) or_return
        }

        // Have exponent form?
        if matches(lexer, "eE") {
            matches(lexer, "+-")
            consume_sequence(lexer, match.is_digit) or_return
        }
        // Consume trailing characters so we can tell if this is a bad number.
        return consume_sequence(lexer, is_alnum)
    }

    if error = consume_number(lexer, prev); error != nil{
        return create_token(lexer, .Error), error
    }
    token = create_token(lexer, .Number)
    ok: bool
    lexer.number, ok = strconv.parse_f64(token.lexeme)
    return token, nil if ok else .Malformed_Number
}

@(private="file", require_results)
create_string_token :: proc(lexer: ^Lexer, quote: rune) -> (token: Token, error: Lexer_Error) {
    builder := &lexer.vm.builder
    strings.builder_reset(builder)

    builder_loop: for !is_at_end(lexer^) && peek(lexer^) != quote {
        r, read_err := advance(lexer)
        if read_err != nil {
            error = read_err
            break builder_loop
        } else if r == '\n' {
            error = .Unterminated_String
            break builder_loop
        }
        // If have escape character, skip it and read the escaped sequence
        if r == '\\' {
            r, read_err = advance(lexer)
            if read_err != nil {
                error = read_err
                break builder_loop
            }
            switch r {
            case 'a':   r = '\a'
            case 'b':   r = '\b'
            case 'f':   r = '\f'
            case 'n':   r = '\n'
            case 'r':   r = '\r'
            case 't':   r = '\t'
            case 'v':   r = '\v'
            case '\\':  r = '\\'
            case '\'':  r = '\''
            case '\"':  r = '\"'
            }
        }
        strings.write_rune(builder, r)
    }
    if is_at_end(lexer^) || !matches(lexer, quote){
        error = .Unterminated_String
    }
    lexer.str = ostring_new(lexer.vm, strings.to_string(builder^))
    return create_token(lexer, .String), error
}

@(private="file")
is_alpha :: proc(r: rune) -> bool {
    return r == '_' || unicode.is_alpha(r)
}

@(private="file")
is_alnum :: proc(r: rune) -> bool {
    return match.is_digit(r) || is_alpha(r)
}

@(private="file", require_results)
consume_sequence :: proc(lexer: ^Lexer, $callback: proc(r: rune) -> bool) -> (error: Lexer_Error) {
    // The 'is_at_end' check is VERY important in case we hit <EOF> here!
    for !is_at_end(lexer^) && callback(peek(lexer^)) {
        advance(lexer) or_return
    }
    return nil
}


@(private="file", require_results)
consume_whitespace :: proc(lexer: ^Lexer) -> (error: Lexer_Error) {

    @(require_results)
    check_multiline :: proc(lexer: ^Lexer) -> (opening: int, is_multiline: bool) {
        if !matches(lexer, '[') {
            return 0, false
        }
        opening      = count_nesting(lexer)
        is_multiline = matches(lexer, '[')
        return opening, is_multiline
    }

    @(require_results)
    skip_comment :: proc(lexer: ^Lexer) -> (error: Lexer_Error) {
        // Skip the 2 '-'.
        advance(lexer)
        advance(lexer)
        if opening, is_multiline := check_multiline(lexer); is_multiline {
            // Don't return yet if successful; may still have newlines.
            consume_multiline(lexer, opening, is_comment = true) or_return
        } else {
            consume_sequence(lexer, proc(r: rune) -> bool { return r != '\n' }) or_return
        }
        return nil
    }

    for {
        switch peek(lexer^) {
        case '\n':
            lexer.line += 1;
            fallthrough
        case ' ', '\r', '\t':
            // Ensure start points to the first actual character in the lexeme.
            lexer.start += 1
            advance(lexer)
        case '-':
            if peek_next(lexer^) != '-' {
                return nil
            }
            skip_comment(lexer) or_return
        case:
            return nil
        }
    }
}

@(private="file", require_results)
consume_multiline :: proc(lexer: ^Lexer, opening: int, $is_comment: bool) -> (error: Lexer_Error) {
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
            return .Unterminated_Multiline
        }

        if peek(lexer^) == '\n' {
            lexer.line += 1
        }
        advance(lexer) or_return
    }
    return nil
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
advance :: proc(lexer: ^Lexer) -> (r: rune, error: Lexer_Error) {
    rr, size := utf8.decode_rune(lexer.input[lexer.current:])
    lexer.current += size
    return rr, .Bad_Rune if rr == utf8.RUNE_ERROR else nil
}

@(private="file")
matches :: proc {
    match_rune,
    match_any,
}

@(private="file")
match_rune :: proc(lexer: ^Lexer, want: rune) -> (found: bool) {
    found = peek(lexer^) == want
    if found {
        advance(lexer)
    }
    return found
}

@(private="file")
match_any :: proc(lexer: ^Lexer, want: string) -> (found: bool) {
    found = strings.index_rune(want, peek(lexer^)) != -1
    if found {
        advance(lexer)
    }
    return found
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
