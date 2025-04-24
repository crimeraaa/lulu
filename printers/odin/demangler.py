from __future__ import annotations
from typing import Final, TypeAlias, Optional, Literal
from dataclasses import dataclass
import re

Usertype: TypeAlias = Literal["struct", "enum", "union"]
Builtin:  TypeAlias = Literal["slice", "array", "dynamic", "map"]

@dataclass
class Declaration:
    prefix:     Optional[Usertype]
    map:        Optional[Literal["map"]]
    brackets:   Optional[Literal["[]", "[dynamic]"] | str]
    inner:      Optional[str]
    package:    Optional[str]
    file:       Optional[str]
    name:       str
    pointer:    Optional[str]


    def __str__(self) -> str:
        result = ""
        if p := self.pointer:   result += '^' * len(p.strip())
        if m := self.map:       result += m
        if b := self.brackets:  result += b
        if p := self.package:   result += p + '.'
        result += self.name # MUST exist
        return result


class Demangled:
    decl: Declaration
    mode: Optional[Usertype | Builtin]


    def __init__(self, line: str):
        self.line = line
        self.decl = Declaration(**MANGLED_PATTERN.match(line).groupdict())
        if self.decl.map:
            self.mode = "map"
        elif b := self.decl.brackets:
            if b == "[]":
                self.mode = "slice"
            elif b == "[dynamic]":
                self.mode = "dynamic"
            else:
                self.mode = "array"
        else:
            self.mode = self.decl.prefix


MANGLED_PATTERN: Final = re.compile(
    # \1 ::= ( ( "struct"|"enum"|"union" ) ' ' )
    # C aggregate type prefix, if any; strip away trailing space if found.
    # Note that of the `(P...)` extensions only named patterns are captured.
    r'^(?P<prefix>(struct|enum|union))?(?(prefix)\s)'

    r'(?P<map>map)?'          # \2 ::= "map" | None
    r'(?P<brackets>\['        # \3 ::= '[' \4 ']' | None
        r'(?P<inner>[^\]]+)?' # \4 ::= "dynamic" | <basic-type> | None
    r'\])?'

    r'(?P<package>[_\w]+)?(?(package)::)' # \5 ::= <ident> "::" | None
    r'(?P<file>\[[^.]+\.odin\])?(?(file)::)' # \6 ::= '[' <ident> ']' "::" | None
    r'(?P<name>[^\s]+)' # \7 ::= <type>
    r'(?P<pointer>(?: )[*]+)?' # \8 ::= ' ' [*]+ | None
)


__known_decls: dict[str, tuple[Demangled, str]] = {}


def demangle(line: str) -> Optional[tuple[Demangled, str]]:
    global __known_decls
    if line in __known_decls:
        return __known_decls[line]

    try:
        demangled = Demangled(line)
        __known_decls[line] = (demangled, str(demangled.decl))
        return __known_decls[line]
    except:
        return None, ""


if __name__ == "__main__":
    from traceback import print_exc
    import readline # Not used directly; but affects how `input()` works.

    print("Enter a mangled Odin type declaration to demangle.")
    while True:
        try:
            line = input(">>>")
            decl, tag = demangle(line)
            if decl and tag:
                print(tag)
            else:
                print(f"Invalid declaration '{line}'.")
        except (KeyboardInterrupt, EOFError):
            print()
            break
        except:
            print_exc()
            continue
