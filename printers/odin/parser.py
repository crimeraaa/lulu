"""
NOTE(2025-04-25):
-   Due to our use of relative imports you cannot run this as a script!
-   Use the `-m` flag to run as a module; e.g. `python -m printers.odin.parser`
    if running from the top-level `lulu` directory.
-   If CWD is `lulu/printers` then run `python -m odin.parser`.
"""
from __future__ import annotations
from dataclasses import dataclass
from typing import Final, Optional, TypeAlias, Literal, Callable
from .lexer import Token, Lexer

Usertype: TypeAlias = Literal["struct", "enum", "union"]
Builtin:  TypeAlias = Literal["slice", "array", "dynamic", "map"]

@dataclass
class Declaration:
    prefix:     Optional[Usertype] = None
    struct:     Optional[Literal["map", "[]", "[dynamic]"]] = None
    info:       Optional[str] = None # e.g. `[K]` in `map[K]`, `T` in `[]T` and `[dynamic]T`
    package:    Optional[str] = None
    file:       Optional[str] = None
    name:       str           = ""
    size:       Optional[int] = None # For fixed-size arrays only
    pointer:    int           = 0 # Levels of indirection directly attached.


    def __str__(self) -> str:
        result: list[str] = []
        if p := self.pointer:   result.append('^' * p)
        if s := self.struct:    result.append(s)
        if i := self.info:      result.append(i)
        if p := self.package:   result.append(p + '.')
        result.append(self.name) # MUST exist
        return ''.join(result)


@dataclass
class Demangled:
    decl:   Declaration = None
    mode:   Optional[Usertype | Builtin] = None
    nested: bool = False


    def recurse(self, expr: Callable[[], None]):
        self.nested = True
        expr()
        self.nested = False

    def set_package(self, package: str):
        if not self.nested:
            self.decl.package = package


    def set_file(self, file: str):
        if not self.nested:
            self.decl.file = file


    def set_name(self, name: str):
        if not self.nested:
            self.decl.name = name


    def set_size(self, size: int):
        if not self.nested:
            self.decl.size = size


    # When recursing, assume the outermost call was the one who initialized it.
    def add_info(self, tokens: str):
        if self.decl.info:
            self.decl.info += tokens
        else:
            self.decl.info = tokens


    def add_pointer(self, count = 1):
        self.decl.pointer += count


    def set_prefix(self, prefix: Usertype):
        if prev := self.decl.prefix:
            raise ValueError(
                f"Already have decl.prefix={quote(prev)}; got {quote(prefix)}")
        self.decl.prefix = prefix


    def set_struct(self, struct: str, mode: str):
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
        if self.nested:
            return

        if prev := self.decl.struct:
            raise ValueError(
                f"Already have decl.struct={quote(prev)}; got {quote(struct)}")

        self.decl.struct = struct
        self.mode        = mode



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


Result: TypeAlias = tuple[Demangled, str]


def demangle(parser: Parser, decl: str, saved: dict[str, Result]) -> Result:
    if decl in saved:
        return saved[decl]

    demangled = Demangled(decl = Declaration())
    parser.set_input(decl)
    parser.next()
    fulltype(parser, demangled)

    saved[decl] = demangled, str(demangled.decl)
    return saved[decl]


def fulltype(parser: Parser, demangled: Demangled):
    """
    ```
    <full-type>     ::= ( <prefix> ' ' )? <compound>? <qualname> ( ' ' <trailing> )?
    <trailing>      ::= <c-pointer>
                        | <array-pointer>? ( '[' <int> ']' )+
    <c-pointer>     ::= '*'+
    <array-pointer> ::= '(' <c-pointer> ')'
    ```
    """
    prefix(parser, demangled)
    compound(parser, demangled)
    qualname(parser, demangled)

    # I am NOT dealing with ridiculous C array-pointer declarations
    # The main issue is that since fixed-size arrays themselves fit in C,
    # pointers-within and pointers-to do not get mangled to Odin's pointers.
    c_pointer(parser, demangled)


def c_pointer(parser: Parser, demangled: Demangled):
    count = 0
    while parser.match(Token.Type.Asterisk):
        count += 1
    demangled.add_pointer(count)


def prefix(parser: Parser, demangled: Demangled):
    """
    ```
    <prefix> ::= "struct" | "enum" | "union"
    ```

    Notes:
    -   We do not allow `map` without a preceding `struct` for our purposes.
    """
    VALID: Final = {"struct", "union", "enum"}

    if parser.check(Token.Type.Keyword):
        demangled.set_prefix(parser.keyword(VALID))


def qualname(parser: Parser, demangled: Demangled) -> str:
    """
    ```
    <qualname>  ::= <namespace>? <ident>
    <namespace> ::= <package> <file>?
    <package>   ::= <ident> '::'
    <file>      ::= '[' <ident> ']' '::'
    <ident>     ::= r'[-_.\w]+'
    ```
    """
    tokens: list[str] = []
    ident = parser.consume(Token.Type.Ident)
    tokens.append(ident)
    # Have a package namespace?
    if parser.match(Token.Type.Delim):
        demangled.set_package(ident)
        # Have a file sub-namespace?
        if parser.match(Token.Type.Left_Bracket):
            ident = parser.consume(Token.Type.Ident)
            # tokens.append(f"[{ident}]") # Not present in Odin-facing decl
            parser.consume(Token.Type.Right_Bracket)
            parser.consume(Token.Type.Delim)
            demangled.set_file(ident)

        # A lone type name ALWAYS follows the namespacing.
        ident = parser.consume(Token.Type.Ident)
        tokens.append(ident)

    demangled.set_name(ident)
    return '.'.join(tokens)


def compound(parser: Parser, demangled: Demangled):
    """
    ```
    <compound>  ::= <header> '^'* <compound>*
    <header>    ::= '[' ( <int> | "dynamic" | '^' )? ']'
                    | "map" '[' <qualname> ']'
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

    tokens: list[str] = []

    # <array-like>
    if parser.match(Token.Type.Left_Bracket):
        tokens.append('[')
        array: Optional[int] = None

        # Fixed-size array?
        if parser.match(Token.Type.Integer):
            array   = int(parser.consumed.data)
            tokens.append(str(array))
            demangled.set_struct(f"[{array}]", "array")
            tokens.clear()

        # Multi-pointer?
        elif parser.match(Token.Type.Caret):
            """
            NOTE(2025-04-25):
            -   This shouldn't be reached for primary calls within GDB.
            -   Multipointers (leftmost) transform to C-style pointers.
            -   `[^]byte` becomes `byte *`
            -   `[^]string` becomes `struct string *`

            -   We should only reach here for recursive calls.
            -   `[][^]byte` becomes `struct [][^]byte`
            -   `[][^]string` becomes `struct [][^]string`
            """
            # `output` is from a recursive call so we don't care about the
            # number of pointers; we just want the demangled type.
            if demangled.nested:
                tokens.append('^')

            # `output` is from the primary call, and the primary type is a
            # multipointer.
            else:
                raise ValueError(
                    "`[^]` in primary types are impossible within GDB")

        # Dynamic array?
        elif parser.check(Token.Type.Keyword):
            mode = parser.keyword("dynamic")
            demangled.set_struct("[dynamic]", mode)
            tokens.clear()

        # Slice?
        else:
            demangled.set_struct("[]", "slice")
            tokens.clear()

        if array:
            demangled.set_size(array)

    # <map> ::= "map" '[' <qualname> ']'
    elif parser.check(Token.Type.Keyword):
        mode = parser.keyword("map")
        tokens.append(parser.consume(Token.Type.Left_Bracket))
        demangled.set_struct(mode, mode)

        """
        NOTE(2025-04-25):
        -   I'm unsure how aliases to basic types are mangled.
        -   e.g. is `core/libc.char` mangled to `libc::char`?
        -   ...or is it immediately resolved to `byte`?
        -   How about `distinct` types?
        """
        demangled.recurse(lambda: tokens.append(qualname(parser, demangled)))

    # Not one of: slice, array, dynamic, map.
    # In this case, `demangled.decl.info` will not be set.
    else:
        return

    rb = parser.consume(Token.Type.Right_Bracket)
    # We haven't yet cleared the tokesn list?
    if len(tokens) > 0:
        tokens.append(rb)

    # Compound-to-pointer, so the Odin-style pointer is already part of the
    # mangled name. e.g. `[]^T`, `map[string]^T` `[dynamic]^T`.
    while parser.match(Token.Type.Caret):
        tokens.append('^')

    demangled.add_info(''.join(tokens))

    # Compound-to-Compound; e.g. `[][]T`, `[8]map[string]T`,
    # `[]^[]T`, `map[string]map[string]T`
    if parser.check(Token.Type.Left_Bracket):
        demangled.recurse(lambda: compound(parser, demangled))


if __name__ == "__main__":
    import traceback
    import readline # Just importing this already affects `input()`.

    parser = Parser()
    saved: dict[str, Result] = {}
    print("Enter an Odin mangled type to parse.")
    while True:
        try:
            decl = input(">>> ")
            demangled, pretty = demangle(parser, decl, saved)
            # print(demangled)
            print(f"Odin: {pretty}")
        except (KeyboardInterrupt, EOFError):
            print()
            break
        except:
            traceback.print_exc()
            continue
