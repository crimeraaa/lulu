from __future__ import annotations
from typing import Final
from enum import StrEnum, unique
from dataclasses import dataclass

@unique
class Token_Type(StrEnum):
    Left_Paren      = '('
    Left_Bracket    = '['
    Right_Paren     = ')'
    Right_Bracket   = ']'
    Dollar          = '$'
    Equal           = '='
    Delim           = "::"
    Asterisk        = '*'
    Caret           = '^'
    Comma           = ','
    Struct          = "struct"
    Enum            = "enum"
    Union           = "union"
    Dynamic         = "dynamic"
    Map             = "map"
    Integer         = "<int>"     # Integer literal: (fixed arrays, parapoly)
    Ident           = "<ident>"   # Package declarations, filenames, type names.
    Unknown         = "<unknown>" # Not a token we know how to deal with!
    Eof             = "<eof>"     # End of input string reached.


@dataclass
class Token:
    type = Token_Type.Eof
    data = ""

    def __repr__(self) -> str:
        return f"{self.type}: '{self.data}'"

    def reset(self):
        self.type = Token_Type.Eof
        self.data = ""

    def copy(self, other: Token):
        self.type = other.type
        self.data = other.data


TOKEN_STRINGS: Final = {t.value: t for t in Token_Type}


class Lexer:

    def __init__(self):
        self.input   = ""
        self.start   = 0
        self.current = 0

    def set_input(self, input: str):
        self.input   = input
        self.start   = 0
        self.current = 0

    def lex(self, token: Token):
        # Skip whitespace
        while self.peek().isspace():
            self.advance()

        if self.is_eof():
            return self.make_token(token)

        self.start = self.current

        # At least a single-character token.
        c = self.advance()
        if c.isalpha():
            c = self.peek()
            # Package names and type names shouldn't have dashes nor periods.
            # However, the compiler should be the one to verify that!
            while c.isalnum() or (c in {'.', '_', '-'}):
                self.advance()
                c = self.peek()

            word = self.lexeme()
            if word in TOKEN_STRINGS:
                ttype = TOKEN_STRINGS[word]
            else:
                ttype = Token_Type.Ident
            return self.make_token(token, ttype)
        elif c.isdecimal():
            c = self.peek()
            while c.isdecimal():
                self.advance()
                c = self.peek()
            return self.make_token(token, Token_Type.Integer)

        if c == ':':
            self.advance()

        if word := self.lexeme():
            if word in TOKEN_STRINGS:
                return self.make_token(token, TOKEN_STRINGS[word])

        # Base-case
        return self.make_token(token, Token_Type.Unknown)

    def make_token(self, token: Token, type = Token_Type.Eof):
        token.type = type
        token.data = self.lexeme()

    def is_eof(self) -> bool:
        return self.current >= len(self.input)

    def peek(self) -> str:
        return "" if self.is_eof() else self.input[self.current]

    def advance(self) -> str:
        c = self.peek()
        self.current += 1
        return c

    def lexeme(self) -> str:
        return self.input[self.start:self.current]

