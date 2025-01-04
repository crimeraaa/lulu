package lulu

import "core:fmt"

compiler_compile :: proc(vm: ^VM, input, name: string) {
    lexer := lexer_create(input, name)
    line  := -1
    tokenize: for {
        token, lex_err := lexer_scan_token(&lexer)
        if lex_err != nil {
            if lex_err != .Eof {
                fmt.printfln("Error: %v @ %q", lex_err, token.lexeme)
            }
            break tokenize
        }
        if (token.line != line) {
            fmt.printf("%4i ", token.line)
            line = token.line
        } else {
            fmt.printf("   | ")
        }
        fmt.println(token)
    }
}
