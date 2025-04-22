from __future__ import annotations
from enum import Enum
from typing import Final
import traceback


class Token_Type(Enum):
    Left_Bracket    = 1 # `[`
    Right_Bracket   = 2 # `]`
    Delim           = 3 # `::` literal
    Asterisk        = 4 # `*`
    Keyword         = 5 # One of `struct`, `enum` or `union`.
    Integer         = 6 # An integer literal, mainly for fixed array types.
    Ident           = 7 # package declarations, filenames, type names.
    Unknown         = 8 # Not a token we know how to deal with!
    Eof             = 9 # End of input string reached.


class Token:
    type: Token_Type
    data: str


    def __init__(self, type = Token_Type.Eof, data = ""):
        self.type = type
        self.data = data


    def __str__(self) -> str:
        if self.data:
            return f"\"{self.data}\""
        else:
            return self.type.name


    def reset(self):
        self.type = Token_Type.Eof
        self.data = ""


    def copy(self, other: Token):
        self.type = other.type
        self.data = other.data


class Lexer:
    """
    Links:
    -   https://docs.python.org/3/library/re.html#writing-a-tokenizer
    """

    input:    str # The string we're tokenizing
    start:    int # Start of lexeme
    current:  int # `Pointer` to current character in lexeme
    keywords: Final = {"struct", "enum", "union"}


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
                return self.make_token(token, Token_Type.Keyword)
            else:
                # Still need to further verify if it's a package name, a file
                # name or a type name.
                return self.make_token(token, Token_Type.Ident)
        elif c.isdecimal():
            c = self.peek()
            while c.isdecimal():
                self.advance()
                c = self.peek()
            return self.make_token(token, Token_Type.Integer)

        match c:
            case ':':
                if self.advance() == ':':
                    return self.make_token(token, Token_Type.Delim)
            case '[': return self.make_token(token, Token_Type.Left_Bracket)
            case ']': return self.make_token(token, Token_Type.Right_Bracket)
            case '*': return self.make_token(token, Token_Type.Asterisk)

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


class Pretty_Type:
    package:    str # Package name, if applicable. If empty, assume builtin.
    file:       str # File name, if private. Assumes `package` is non-empty.
    kind:       str # empty, `struct`, `enum`, `union`
    length:     int # -2: non-array; -1: slice; >= 0: fixed-size array
    tag:        str # Type tag for structures; e.g. `string` in `struct string`.
    __pretty:   str


    def __init__(self):
        self.package    = ""
        self.file       = ""
        self.kind       = ""
        self.length     = -2
        self.tag        = ""
        self.__pretty   = ""


    def __str__(self) -> str:
        # First time calling this?
        if not self.__pretty:
            final: list[str] = []

            # Determine if we should add array notation and how.
            match self.length:
                case -2: pass # Tag is already correct
                case -1: self.tag = f"[]{self.tag}"
                case _:  self.tag = f"[{self.length}]{self.tag}"

            if self.package:      final.append(f"Package: {self.package}")
            if self.file:         final.append(f"File:    {self.file}")
            if self.kind:         final.append(f"Kind:    {self.kind}")
            if self.length >= 0:  final.append(f"Length:  {self.length}")
            final.append(f"Type:    {self.tag}")
            self.__pretty = "\n".join(final)
        return self.__pretty


class Demangler:
    lexer:      Lexer
    consumed:   Token
    lookahead:  Token

    def __init__(self):
        self.lexer      = Lexer()
        self.consumed   = Token()
        self.lookahead  = Token()


    def parse(self, decl: str) -> Pretty_Type:
        self.lexer.set_input(decl)
        self.lexer.lex(self.lookahead)
        self.next()

        pretty = Pretty_Type()
        self.prefix(pretty) # Assume we end up consuming an identifier
        self.infix(pretty, self.consumed.data)
        self.consume(Token_Type.Eof)
        return pretty


    def prefix(self, pretty: Pretty_Type):
        """
        ```
        prefix :=  '[' [ INTEGER ] ']' infix
                |   [ ( 'struct' | 'enum' | 'union' ) ' ' ] prefix
                |   IDENT infix
        ```
        """
        match self.consumed.type:
            case Token_Type.Left_Bracket:
                # Is a fixed-size array?
                if self.match(Token_Type.Integer):
                    pretty.length = int(self.consumed.data, base = 10)
                else:
                    pretty.length = -1

                # TODO(2025-04-22): Account for slices-of-slices? e.g. `[][]u8`
                self.consume(Token_Type.Right_Bracket)
                self.consume(Token_Type.Ident)
            case Token_Type.Keyword:
                pretty.kind = self.consumed.data
                self.consume(Token_Type.Ident)
            case Token_Type.Ident:
                pass
            case _:
                raise ValueError(f"Invalid starting token: {self.consumed}")


    def infix(self, pretty: Pretty_Type, ident: str):
        """
        ```
        infix   ::= [ IDENT '::' [ '[' IDENT ']' '::' ] ] '*'* IDENT
        IDENT   ::= r'[-_\w\.]+'
        ```

        Note:
        -   For simplicity, `IDENT` allows dashes and spaces.
        -   In practice the Odin compiler will disallow identifiers in source
            code with these characters.
        -   However file names should be just fine.
        """

        # `ident` is a package name because it is a public namespace.
        # PACKAGE '::'
        if self.match(Token_Type.Delim):
            pretty.package = ident

            # We have a file name; a private namespace.
            # ::= '[' FILENAME ']' '::'
            if self.match(Token_Type.Left_Bracket):
                self.consume(Token_Type.Ident)
                pretty.file = self.consumed.data
                self.consume(Token_Type.Right_Bracket)
                self.consume(Token_Type.Delim)

            # This should be the raw type name.
            self.consume(Token_Type.Ident)

        # We should now be at the raw type name.
        pretty.tag = self.consumed.data


    def consume(self, type: Token_Type):
        if not self.match(type):
            raise ValueError(f"Expected {type.name}; Got {self.lookahead}")


    def match(self, type: Token_Type) -> bool:
        found = self.check(type)
        if found:
            self.next()
        return found


    def check(self, type: Token_Type) -> bool:
        return self.lookahead.type == type


    def next(self):
        self.consumed.copy(self.lookahead)
        self.lexer.lex(self.lookahead)


if __name__ == "__main__":
    print("\n\t".join(["Enter Odin type names to demangle, e.g:",
        "int", "[]u8", "[256]byte", "struct string",
        "union Value",
        "struct fmt::Info",
        "enum lulu::[value.odin]::Value_Type"]))

    demangler = Demangler()
    while True:
        try:
            mangled = input(">>> ")
            pretty  = demangler.parse(mangled)
            print(pretty)

        except (KeyboardInterrupt, EOFError): # CTRL-C, CTRL-D respectively
            print()
            break

        except Exception: # A bug in our implementation somewhere?
            traceback.print_exc()
            continue
