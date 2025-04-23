from __future__ import annotations
from dataclasses import dataclass
from enum import Enum
from typing import Final


@dataclass
class Token:
    class Type(Enum):
        Left_Paren      =  1 # `(`
        Left_Bracket    =  2 # `[`
        Right_Paren     =  3 # `)`
        Right_Bracket   =  4 # `]`
        Delim           =  5 # `::`
        Asterisk        =  6 # `*`
        Caret           =  7 # `^`
        Keyword         =  8 # One of `struct`, `enum` or `union`.
        Integer         =  9 # An integer literal, mainly for fixed array types.
        Ident           = 10 # package declarations, filenames, type names.
        Unknown         = 11 # Not a token we know how to deal with!
        Eof             = 12 # End of input string reached.


    type: Type = Type.Eof
    data: str  = ""


    def __repr__(self) -> str:
        if self.data:
            return f"{self.type.name}: {self.data}"
        else:
            return self.type.name


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
    keywords:   Final = {"struct", "enum", "union"}
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


@dataclass
class Pretty:
    package:    str = "" # Package name, if applicable. If empty, assume builtin.
    file:       str = "" # File name, if private. Assumes `package` is non-empty.
    kind:       str = "" # empty, `struct`, `enum`, `union`
    length:     int = -2 # -2: non-array; -1: slice; >= 0: fixed-size array
    pointer:    int =  0 # How many levels of indirection?
    tag:        str = "" # Type tag for structures; e.g. `string` in `struct string`.
    __final:    str = ""


    def __repr__(self) -> str:
        # First time calling this?
        if not self.__final:
            final: list[str] = []

            if self.package:      final.append(f"Package: {self.package}")
            if self.file:         final.append(f"File:    {self.file}")
            if self.kind:         final.append(f"Kind:    {self.kind}")
            if self.length >= 0:  final.append(f"Length:  {self.length}")

            final.append(f"Tag:     {self.tag}")
            self.__final = "\n".join(final)
        return self.__final


class Demangler:
    lexer:      Lexer
    consumed:   Token
    lookahead:  Token

    def __init__(self):
        self.lexer      = Lexer()
        self.consumed   = Token()
        self.lookahead  = Token()


    def parse(self, decl: str) -> Pretty:
        self.lexer.set_input(decl)
        self.lexer.lex(self.lookahead)
        self.next()

        pretty = Pretty()
        if self.prefix(pretty):
            self.infix(pretty)
        self.pointer(pretty)
        self.consume(Token.Type.Eof)

        stars = '*' * pretty.pointer
        match pretty.length:
            case -2:
                # TODO(2025-04-23): Distinguish slices of pointers?
                # e.g. `[]^Value` is distinct from `[]Value *`.
                if stars: pretty.tag += ' ' + stars
            case -1:
                # Pointer *to* a slice? e.g. `[](*)u8`
                # Slices of pointers use Odin pointer-syntax; e.g. `[]^Object`.
                if stars: pretty.tag += f"({stars})"
            case _:
                # Pointer *to* an array? e.g. `[256](*)byte`
                if stars: pretty.tag += f"({stars})"
        return pretty


    def prefix(self, pretty: Pretty) -> bool:
        """
        ```
        # Nonterminals
        prefix  ::= slice
                  | KEYWORD prefix
                  | IDENT '::'?
        slice   ::= '[' INTEGER ']' prefix

        # Terminals
        INTEGER ::= r'\d+'
        KEYWORD ::= ( 'struct' | 'enum' | 'union' ) ' '
        ```

        Returns:
        -   `True` if there is more to parse, else `False`.
        """
        match self.consumed.type:
            # May also apply to slice-of-pointers, e.g. `[]^Value`
            # NOTE(2025-04-23): Pointer to above is `[]^Value(*)`.
            case Token.Type.Caret:
                pretty.tag += '^'
                self.next()
                while self.match(Token.Type.Caret):
                    pretty.tag += '^'
                return self.prefix(pretty)

            case Token.Type.Left_Bracket:
                return self.parse_slice(pretty)

            case Token.Type.Keyword:
                kind = self.consumed.data
                # Should not occur with types already verified by Odin.
                if pretty.kind:
                    raise ValueError(f"Type is already a {pretty.kind}; got {kind}")

                pretty.kind = kind
                self.next()
                return self.prefix(pretty)

            case Token.Type.Ident:
                ident = self.consumed.data
                if self.match(Token.Type.Delim):
                    # Should not occur with types already verified by Odin.
                    if pretty.package:
                        raise ValueError(f"Already have package {pretty.package}; got {ident}")

                    pretty.package = ident
                    return True
                else:
                    pretty.tag += ident
                    return False
            case _:
                raise ValueError(f"Invalid starting token: {self.consumed}")

        return False


    def parse_slice(self, pretty: Pretty) -> bool:
        """
        Returns:
        -   `True` if there is more to parse, else `False`.
        """
        # Is a fixed-size array?
        if self.match(Token.Type.Integer):
            size = int(self.consumed.data, base = 10)
            pretty.length  = size
            pretty.tag    += f"[{size}]"
        else:
            pretty.length = -1
            pretty.tag    += "[]"

        self.consume(Token.Type.Right_Bracket)
        # Should also account for Slice/Array of Slice/Array? e.g. `[][]u8`.
        self.next()
        return self.prefix(pretty)


    def infix(self, pretty: Pretty):
        """
        ```
        infix   ::= private? IDENT
        private ::= '[' IDENT ']' '::'
        IDENT   ::= r'[-_\w\.]+'
        ```

        Note:
        -   For simplicity, `IDENT` allows dashes and spaces.
        -   In practice the Odin compiler will disallow packages and identifiers
            with these identifiers in source code.
        -   However file names should be just fine.
        """

        # We have a file name; a private namespace.
        if self.match(Token.Type.Left_Bracket):
            self.consume(Token.Type.Ident)
            pretty.file = self.consumed.data
            self.consume(Token.Type.Right_Bracket)
            self.consume(Token.Type.Delim)

        self.consume(Token.Type.Ident)

        # We should now be at the raw type name; the 'tag'.
        pretty.tag += self.consumed.data


    def pointer(self, pretty: Pretty):
        """
        Overview
        -   Consumes C-style pointer declarations `*`.
        """
        while self.match(Token.Type.Asterisk):
            pretty.pointer += 1


    def consume(self, type: Token.Type):
        if not self.match(type):
            raise ValueError(f"Expected {type.name}; Got {self.lookahead}")


    def match(self, type: Token.Type) -> bool:
        found = self.check(type)
        if found:
            self.next()
        return found


    def check(self, type: Token.Type) -> bool:
        return self.lookahead.type == type


    def next(self):
        self.consumed.copy(self.lookahead)
        self.lexer.lex(self.lookahead)


if __name__ == "__main__":
    import traceback

    SAMPLES: Final = [
        "", # Ensure first non-empty element is newline-tabbed as well
        "int", "[]u8", "[256]byte", "struct string", # builtin
        "union Value", "enum Value_Type", # user-defined, not namespaced
        "struct fmt::Info",
        "enum lulu::[value.odin]::Value_Type",
        "struct []lulu::[table.odin]::Table_Entry *",

        # Unsupported
        # "struct [dynamic]byte",
        # "struct map[string]^lulu::[string.odin]::OString",
        # "struct [dynamic]u8 *",
    ]

    print("Enter Odin type names to demangle, e.g:", "\n\t".join(SAMPLES))

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
