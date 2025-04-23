from dataclasses import dataclass
from lexer import Token, Lexer
from typing import Final, Optional


@dataclass
class Demangled:
    kind:    str = ""
    package: str = ""
    file:    str = ""
    size:    Optional[int] = None # For fixed-size arrays only
    pointer: int = 0  # Levels of indirection attached DIRECTLY to the tag.
    prev:    bool = False # For parsing compound types of compound types
    tag:     str = "" # Use empty strings so we can directly append


    def __repr__(self) -> str:
        s: list[str] = []
        if self.kind:               s.append(f"kind={quote(self.kind)}")
        if self.package:            s.append(f"package={quote(self.package)}")
        if self.file:               s.append(f"file={quote(self.file)}")
        if self.size is not None:   s.append(f"array={self.size}")
        if self.pointer:            s.append(f"pointer={self.pointer}")
        if self.tag:                s.append(f"tag={quote(self.tag)}")
        return f"Demangled({', '.join(s)})"


    def set_kind(self, kind: str):
        """
        Note:
        -   When recursively parsing, `self.prev` is set to `True`.
        -   That means we are not in the primary call.
        -   In that case, instances of `map`, `dynamic`, etc. describe the
            array/map elements, not the container.

        Examples:
        -   `struct map[string][dynamic]int`
        -   `struct [][]byte`
        -   `struct [dynamic][]string`
        """
        if not self.prev:
            self.kind = kind


class Parser:
    lexer:     Lexer
    consumed:  Token
    lookahead: Token


    def __init__(self):
        self.lexer     = Lexer()
        self.consumed  = Token()
        self.lookahead = Token()


    def set_input(self, decl: str):
        self.lexer.set_input(decl)


    def match(self, expected: Token.Type) -> bool:
        found = self.lookahead.type == expected
        if found:
            self.next()
        return found


    def check(self, expected: Token.Type) -> bool:
        return self.lookahead.type == expected


    def keyword(self, expected: str | list[str] | set[str]) -> str:
        keyword = self.consume(Token.Type.Keyword)
        # `in` keyword applies to `list`, `set` and `str`
        if keyword in expected:
            return keyword

        parser.unexpected(expected, keyword)


    def consume(self, expected: Token.Type) -> str:
        if not self.match(expected):
            if isinstance(expected.value, str):
                exp = quote(expected.value)
            else:
                exp = f"<{expected.name.lower()}>"
            self.unexpected(exp, repr(self.lookahead))
        return self.consumed.data


    def next(self):
        self.consumed.copy(self.lookahead)
        self.lexer.lex(self.lookahead)


    def unexpected(self, expected: str | list[str] | set[str], culprit: str):
        if isinstance(expected, (list, set)):
            expected = '|'.join([quote(text) for text in expected])
        else:
            expected = quote(expected)
        raise ValueError(f"Expected {expected}; got {culprit}")


def quote(text: str) -> str:
    """
    Overview
    -   C-style quoting.
    -   Single-length strings use single quotes, `'`, to mimic `char`.
    -   All other strings, including the empty string, use double quotes to
        mimic C-string literals: `""`, `"Hi mom!"`
    """
    quote = '\'' if len(text) == 1 else '\"'
    return quote + text + quote


def parse(parser: Parser, decl: str) -> Demangled:
    output = Demangled()
    parser.set_input(decl)
    parser.next()
    fulltype(parser, output)
    return output


def fulltype(parser: Parser, output: Demangled):
    """
    ```
    <full-type>     ::= ( <aggregate> ' ' )? <tag> ( ' ' <trailing> )?
    <trailing>      ::= <c-pointer>
                        | <array-pointer>? ( '[' <int> ']' )+
    <c-pointer>     ::= '*'+
    <array-pointer> ::= '(' <c-pointer> ')'
    ```
    """

    """
    []^[]Value          struct []^[]lulu::[value.odin]::Value
    ^Value              struct lulu::[value.odin]::Value *
    [^]Value            struct lulu::[value.odin]::Value *
    []^Value            => struct []^lulu::[value.odin]::Value
    ^[]Value            => struct []lulu::[value.odin]::Value *
    []Value             => struct []lulu::[value.odin]::Value
    [][]Value           => struct [][]lulu::[value.odin]::Value
    map[string]Value    => struct map[string]lulu::[value.odin]::Value
    ^map[string]Value   => struct map[string]lulu::[value.odin]::Value *
    map[string]^OString => struct map[string]^lulu::[string.odin]::OString

    [256]byte           => u8 [256]
    ^[256]byte          => u8 (*)[256]
    []byte              => struct []u8
    ^[]byte             => struct []u8 *
    ^^[]byte            => struct []u8 **

    [][4]byte           => struct [][4]u8
    ^[][4]byte          => struct [][4]u8 *
    [4][]byte           => struct []u8 [4]
    ^[4][]byte          => struct []u8 (*)[4]
    [4][4]byte          => u8 [4][4]
    ^[4][4]byte         => u8 (*)[4][4]

    [2][3]string        => struct string [2][3]
    [2][3]^string       => struct string *[2][3]

    // I love C's pointer type declarations!
    [2][3]string        => struct string [2][3]
    ^[2][3]string       => struct string (*)[2][3]
    [2][3]^string       => struct string *[2][3]
    ^[2][3]^string      => struct string *(*)[2][3]
    [2]^[3]string       => struct string (*[2])[3]
    ^[2]^[3]string      => struct string (*(*)[2])[3]
    [2]^[3]^string      => struct string *(*[2])[3]
    ^[2]^[3]^string     => struct string *(*(*)[2])[3]
    """
    aggregate(parser, output)
    tag(parser, output)

    # I am NOT dealing with ridiculous C array-pointer declarations
    # The main issue is that since fixed-size arrays themselves fit in C,
    # pointers-within and pointers-to do not get mangled to Odin's pointers.
    count = c_pointer(parser, output)
    if count:
        output.tag += f" {'*' * count}"



def c_pointer(parser: Parser, output: Demangled) -> int:
    count = 0

    while parser.match(Token.Type.Asterisk):
        count += 1

    output.pointer += count
    return count


def aggregate(parser: Parser, output: Demangled):
    """
    ```
    <aggregate> ::= "struct" | "enum" | "union"
    ```

    Notes:
    -   We do not allow `map` without a preceding `struct` for our purposes.
    """
    VALID: Final = {"struct", "union", "enum"}

    if parser.check(Token.Type.Keyword):
        output.kind = parser.keyword(VALID)


def tag(parser: Parser, output: Demangled):
    """
    ```
    <tag>       ::= <compound>? <namespace>? <ident>
    <namespace> ::= <package> <file>?
    <package>   ::= <ident> '::'
    <file>      ::= '[' <ident> ']' '::'
    <ident>     ::= r'[-_.\w]+'
    ```
    """
    compound(parser, output)
    ident = parser.consume(Token.Type.Ident)
    # Have a package namespace?
    if parser.match(Token.Type.Delim):
        output.package = ident
        # Have a file sub-namespace?
        if parser.match(Token.Type.Left_Bracket):
            ident = parser.consume(Token.Type.Ident)
            parser.consume(Token.Type.Right_Bracket)
            parser.consume(Token.Type.Delim)
            output.file = ident

        # A lone type name ALWAYS follows the namespacing.
        ident = parser.consume(Token.Type.Ident)

    output.tag += ident


def compound(parser: Parser, output: Demangled):
    """
    ```
    <compound>  ::= <header> '^'* <compound>*
    <header>    ::= '[' ( <int> | "dynamic" | '^' )? ']'
                    | "map" '[' <tag> ']'
    <int>       ::= r'\d+'
    ```

    Note:
    -   This function does not parse anything after the compound 'header'.
    -   It does not consume `int` in `[]int` nor the second `string` in
        `map[string]string`.
    -   This is the only area where Odin-style pointers `^T` can be seen, as
        they are directly part of the mangled names, e.g. `struct []^int`.
    -   All other pointers are mangled to C-style `T *`.
    """

    # <array-like>
    if parser.match(Token.Type.Left_Bracket):
        output.tag += '['

        array: int | None = None

        # Fixed-size array?
        if parser.match(Token.Type.Integer):
            array = int(parser.consumed.data)
            output.tag += str(array)
            output.set_kind("array")

        # Multi-pointer?
        elif parser.match(Token.Type.Caret):
            parser.consume(Token.Type.Right_Bracket)

            # `output` is from a recursive call so we don't care about the
            # number of pointers; we just want the demangled type.
            if output.prev:
                output.tag += '^'

            # `output` is from the primary call, and the primary type is a
            # multipointer so we shouldn't just add `^` to the tag.
            else:
                output.pointer += 1

        # Dynamic array?
        elif parser.check(Token.Type.Keyword):
            kind = parser.keyword("dynamic")
            output.set_kind(kind)
            output.tag += kind

        # Slice?
        else:
            output.set_kind("slice")

        output.size = array

    # <map> ::= "map" '[' <tag> ']'
    elif parser.check(Token.Type.Keyword):
        kind = parser.keyword("map")
        output.tag += kind
        output.tag += parser.consume(Token.Type.Left_Bracket)

        # Primary call, mark it
        output.set_kind(kind)

        # Semantically, we can only use basic types, or `string`, for map keys.
        # However our parser doesn't know about such semantics!
        tag(parser, output)

    # Not one of: slice, array, dynamic, map.
    else:
        return

    output.tag += parser.consume(Token.Type.Right_Bracket)

    # Compound-to-pointer, so the Odin-style pointer is already part of the
    # mangled name. e.g. `[]^T`, `map[string]^T` `[dynamic]^T`.
    while parser.match(Token.Type.Caret):
        output.tag += '^'

    # Compound-to-Compound; e.g. `[][]T`, `[8]map[string]T`,
    # `[]^[]T`, `map[string]map[string]T`
    if parser.check(Token.Type.Left_Bracket):
        output.prev = True
        compound(parser, output)
        output.prev = False


if __name__ == "__main__":
    import traceback

    parser = Parser()
    print("Enter an Odin mangled type to parse.")
    while True:
        try:
            decl   = input(">>> ")
            output = parse(parser, decl)
            print(output)
        except (KeyboardInterrupt, EOFError):
            print()
            break
        except:
            traceback.print_exc()
            continue
