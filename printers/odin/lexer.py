from __future__ import annotations
from dataclasses import dataclass
from enum import Enum
from typing import Final


@dataclass
class Token:
    class Type(Enum):
        Left_Paren      = '('
        Left_Bracket    = '['
        Right_Paren     = ')'
        Right_Bracket   = ']'
        Delim           = "::"
        Asterisk        = '*'
        Caret           = '^'
        Keyword         = 1 # `struct` | `enum` | `union` | `dynamic` | `map`
        Integer         = 2 # An integer literal, mainly for fixed array types.
        Ident           = 3 # package declarations, filenames, type names.
        Unknown         = 4 # Not a token we know how to deal with!
        Eof             = "<eof>" # End of input string reached.


    type: Type = Type.Eof
    data: str  = ""


    def __repr__(self) -> str:
        # Fixed-size tokens
        if isinstance(self.type.value, str):
            return self.type.value

        # Variable-size tokens
        return f"{self.type}: '{self.data}'"


    def reset(self):
        self.type = Token.Type.Eof
        self.data = ""


    def copy(self, other: Token):
        self.type = other.type
        self.data = other.data


class Lexer:
    """
    Links:
    -   https://docs.python.org/3/library/re.html#writing-a-tokenizer
    """

    input:      str # The string we're tokenizing
    start:      int # Start of lexeme
    current:    int # `Pointer` to current character in lexeme
    keywords:   Final = {"struct", "enum", "union", "dynamic", "map"}
    characters: Final = {
        '(': Token.Type.Left_Paren,
        '[': Token.Type.Left_Bracket,
        ']': Token.Type.Right_Bracket,
        ')': Token.Type.Right_Paren,
        '*': Token.Type.Asterisk,
        '^': Token.Type.Caret,
    }


    def __init__(self):
        self.set_input("")


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

            if self.lexeme() in self.keywords:
                # For our purposes `string`, `int` and such are NOT keyword
                # as they are primarily type names.
                return self.make_token(token, Token.Type.Keyword)
            else:
                # Still need to further verify if it's a package name, a file
                # name or a type name.
                return self.make_token(token, Token.Type.Ident)
        elif c.isdecimal():
            c = self.peek()
            while c.isdecimal():
                self.advance()
                c = self.peek()
            return self.make_token(token, Token.Type.Integer)

        if c == ':':
            if self.advance() == ':':
                return self.make_token(token, Token.Type.Delim)
        elif c in self.characters:
            return self.make_token(token, self.characters[c])

        # Base-case
        return self.make_token(token, Token.Type.Unknown)


    def make_token(self, token: Token, type = Token.Type.Eof):
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

