"""
NOTE(2025-04-25):
-   Due to our use of relative imports you cannot run this as a script!
-   Use the `-m` flag to run as a module; e.g. `python -m printers.odin.parser`
    if running from the top-level `lulu` directory.
-   If CWD is `lulu/printers` then run `python -m odin.parser`.
"""
from dataclasses import dataclass
from typing import Optional

from .lexer import Token, Token_Type, Lexer
from .declaration import Declaration, quote


class ExpectedError(ValueError):
    expected: tuple[Token_Type, ...]
    culprit:  Token

    def __init__(self, *expected: Token_Type, culprit: Token):
        self.expected = expected
        self.culprit  = culprit
        expstr = '|'.join([quote(token.value) for token in expected])
        culstr = quote(repr(culprit))
        super().__init__(f"Expected {expstr}; got {culstr}")


@dataclass
class Parser:
    lexer       = Lexer()
    consumed    = Token()
    lookahead   = Token()

    def set_input(self, decl: str):
        self.lexer.set_input(decl)
        self.next()

    def match(self, expected: Token_Type) -> bool:
        found = self.lookahead.type == expected
        if found:
            self.next()
        return found

    def next(self):
        self.consumed.copy(self.lookahead)
        self.lexer.lex(self.lookahead)

    def match_any(self, *expected: Token_Type) -> bool:
        found = self.lookahead.type in expected
        if found:
            self.next()
        return found

    def check(self, expected: Token_Type) -> bool:
        return self.lookahead.type == expected

    def consume(self, expected: Token_Type) -> str:
        if not self.match(expected):
            raise ExpectedError(expected, culprit=self.lookahead)
        return self.consumed.data

    def expected(self, *expected: Token_Type):
        raise ExpectedError(*expected, culprit=self.lookahead)


def demangle(parser: Parser, mangled: str, saved: dict[str, str]) -> tuple[str, bool]:
    # Parsing is a rather involved process, so if we already know the
    # demangled version of this `mangled` then reuse it!
    if mangled in saved:
        return saved[mangled], True

    try:
        decl = Declaration()
        parser.set_input(mangled)
        fulltype(parser, decl)
    except ExpectedError:
        # Wasn't valid, but save it anyway so we don't have to parse again
        saved[mangled] = mangled
        return mangled, False

    demangled      = str(decl)
    saved[mangled] = demangled
    return demangled, True


def fulltype(parser: Parser, decl: Declaration):
    """
    ```
    <full-type>     ::= ( <prefix> ' ' )? <compound>? <qualname> ( ' ' <trailing> )?
    <trailing>      ::= <c-pointer>
                        | <array-pointer>? ( '[' <int> ']' )+
    <c-pointer>     ::= '*'+
    <array-pointer> ::= '(' <c-pointer> ')'
    ```
    """
    prefix(parser, decl)
    compound(parser, decl)
    qualname(parser, decl)

    # I am NOT dealing with ridiculous C array-pointer declarations
    # The main issue is that since fixed-size arrays themselves fit in C,
    # pointers-within and pointers-to do not get mangled to Odin's pointers.
    c_pointer(parser, decl)


def c_pointer(parser: Parser, decl: Declaration):
    count = 0
    while parser.match(Token_Type.Asterisk):
        count += 1
    decl.add_pointer(count)


def prefix(parser: Parser, decl: Declaration):
    """
    ```
    <prefix> ::= "struct" | "enum" | "union"
    ```

    Notes:
    -   We do not allow `map` without a preceding `struct` for our purposes.
    """
    if parser.match_any(Token_Type.Struct, Token_Type.Enum, Token_Type.Union):
        decl.set_prefix(parser.consumed.data)


def qualname(parser: Parser, decl: Declaration) -> str:
    """
    ```
    <qualname>  ::= <namespace>? <ident> <parapoly>?
    <namespace> ::= <package> <file>?
    <package>   ::= <ident> '::'
    <file>      ::= '[' <ident> ']' '::'
    <ident>     ::= r'[-_.\\w]+'
    <parapoly>  ::= '(' <polyargs> ')'
    ```
    """
    ident = parser.consume(Token_Type.Ident)
    # Have a package namespace?
    if parser.match(Token_Type.Delim):
        decl.set_package(ident)
        # Have a file sub-namespace?
        if parser.match(Token_Type.Left_Bracket):
            ident = parser.consume(Token_Type.Ident)
            # tokens.append(f"[{ident}]") # Not present in Odin-facing decl
            parser.consume(Token_Type.Right_Bracket)
            parser.consume(Token_Type.Delim)
            decl.set_file(ident)

        # A lone type name ALWAYS follows the namespacing.
        ident = parser.consume(Token_Type.Ident)

    decl.set_name(ident)

    # Parapoly instantiation?
    # - `Map_Cell($T=string)`
    # - `Small_Array($T=u16,$N=200)`
    if parser.match(Token_Type.Left_Paren):
        polyargs(parser, decl)
        parser.consume(Token_Type.Right_Paren)

    return str(decl)


def polyargs(parser: Parser, decl: Declaration):
    """
    ```
    <polyargs>  ::= '$' <ident> '=' <funcarg>
    <funcarg>   ::= <qualname> | <literal>
    <literal>   ::= <int> | <str> | 'true' | 'false' | 'nil'
    ```

    Note:
    -   It's entirely possible for a parapoly argument to itself be a
        parapoly instantiation, hence we call `qualname()` recursively.
    -   e.g. `small_array::Small_Array($T=mygame::Vector3($T=f32), $N=4)`
    """
    while True:
        parser.consume(Token_Type.Dollar)
        tparam = parser.consume(Token_Type.Ident)
        parser.consume(Token_Type.Equal)

        if parser.check(Token_Type.Ident):
            tmp = Declaration()
            decl.add_polyarg(tparam, qualname(parser, tmp))
        elif parser.check(Token_Type.Integer):
            decl.add_polyarg(tparam, parser.consume(Token_Type.Integer))
        else:
            parser.expected(Token_Type.Ident, Token_Type.Integer)

        if not parser.match(Token_Type.Comma):
            return


def compound(parser: Parser, decl: Declaration):
    """
    ```
    <compound>  ::= <header> '^'* <compound>*
    <header>    ::= '[' ( <int> | "dynamic" | '^' )? ']'
                    | "map" '[' <qualname> ']'
    <int>       ::= r'\\d+'
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
    if parser.match(Token_Type.Left_Bracket):
        tokens.append('[')
        array: Optional[int] = None

        # Fixed-size array?
        if parser.match(Token_Type.Integer):
            array = int(parser.consumed.data)
            tokens.append(str(array))
            decl.set_odintype(f"[{array}]")
            tokens.clear()

        # Multi-pointer?
        elif parser.match(Token_Type.Caret):
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
            tokens.append('^')

        # Dynamic array?
        elif parser.match(Token_Type.Dynamic):
            decl.set_odintype("[dynamic]")
            tokens.clear()

        # Slice?
        else:
            decl.set_odintype("[]")
            tokens.clear()

        if array:
            decl.set_size(array)

    # <map> ::= "map" '[' <qualname> ']'
    elif parser.match(Token_Type.Map):
        tokens.append(parser.consume(Token_Type.Left_Bracket))
        decl.set_odintype("map")

        """
        NOTE(2025-04-24):
        -   Non-`distinct` type aliases are NOT mangled.
        -   e.g. `core/c.char` is resolved to `u8`.
        -   So `map[c.char][^]c.char` becomes `struct map[u8][^]u8`.

        -   However, `distinct` types ARE mangled.
        -   e.g. `mode :: distinct char` is mangled to `pkg::mode`
        -   `asciiz :: distinct [^]c.char` is mangled to `pkg::asciiz`
        -   `pkg` is whatever package this declaration was found in.
        -   `map[mode]asciiz` becomes `struct map[pkg::mode]pkg::asciiz`.
        """
        key_decl = Declaration()
        tokens.append(qualname(parser, key_decl))

    # Not one of: slice, array, dynamic, map.
    # In this case, `decl.info` will not be set.
    else:
        return

    rb = parser.consume(Token_Type.Right_Bracket)
    # We haven't yet cleared the tokesn list?
    if tokens:
        tokens.append(rb)

    # Compound-to-pointer, so the Odin-style pointer is already part of the
    # mangled name. e.g. `[]^T`, `map[string]^T` `[dynamic]^T`.
    while parser.match(Token_Type.Caret):
        tokens.append('^')

    if tokens:
        decl.add_info(tokens)

    # Compound-to-Compound; e.g. `[][]T`, `[8]map[string]T`,
    # `[]^[]T`, `map[string]map[string]T`
    if not parser.check(Token_Type.Ident):
        value_decl = Declaration()
        compound(parser, value_decl)
        decl.add_info(value_decl)


if __name__ == "__main__":
    import traceback
    import readline # Just importing this already affects `input()`.

    __parser = Parser()
    __saved: dict[str, str] = {}
    print("Enter an Odin mangled type to parse.")
    while True:
        try:
            mangled   = input(">>> ")
            demangled = demangle(__parser, mangled, __saved)
            print(f"Odin: {demangled}")
        except (KeyboardInterrupt, EOFError):
            print()
            break
        except:
            traceback.print_exc()
            continue
