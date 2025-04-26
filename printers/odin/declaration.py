from __future__ import annotations
from dataclasses import dataclass, field
from typing import TypeAlias, Literal, Optional

Usertype: TypeAlias = Literal["struct", "enum", "union"]
Builtin:  TypeAlias = Literal["slice", "array", "dynamic", "map"]


@dataclass
class Polyarg:
    param: str
    arg:   str


@dataclass
class Declaration:
    prefix:     Optional[Usertype] = None
    odintype:   Optional[Literal["map", "[]", "[dynamic]"]] = None
    info:       str           = "" # e.g. `[K]` in `map[K]`, `T` in `[]T` and `[dynamic]T`
    package:    Optional[str] = None
    file:       Optional[str] = None
    name:       str           = ""

    # Ugh https://docs.python.org/3/library/dataclasses.html#dataclasses.field
    parapoly:   list[Polyarg] = field(default_factory=list)
    size:       Optional[int] = None # For fixed-size arrays only
    pointer:    int           = 0    # Levels of indirection directly attached.

    def __str__(self) -> str:
        result: list[str] = []
        if self.pointer:  result.append('^' * self.pointer)
        if self.odintype: result.append(self.odintype)
        if self.info:     result.append(self.info)
        if self.package:  result.append(self.package + '.')

        result.append(self.name) # MUST exist
        if self.parapoly:
            polyargs = [f"${p.param}={p.arg}" for p in self.parapoly]
            # e.g. `struct test::Array($T=u16, $N=4)`
            result.append(f"({', '.join(polyargs)})")
        return ''.join(result)


@dataclass
class Demangled:
    decl: Declaration = field(default_factory=Declaration)
    mode: Optional[Usertype | Builtin] = None
    
    def set_package(self, package: str):
        self.decl.package = package

    def set_file(self, file: str):
        self.decl.file = file

    def set_name(self, name: str):
        self.decl.name = name

    def set_size(self, size: int):
        self.decl.size = size

    def add_info(self, info: list[str] | Demangled):
        if isinstance(info, Demangled):
            self.decl.info += str(info.decl)
        elif isinstance(info, list):
            self.decl.info += ''.join(info)
        else:
            raise TypeError(
                f"Expected `Demangled` or `list[str]`; got `{type(info)}`")

    def add_pointer(self, count = 1):
        self.decl.pointer += count
    
    def add_polyarg(self, tparam: str, targ: str):
        self.decl.parapoly.append(Polyarg(param=tparam, arg=targ))

    def set_prefix(self, prefix: Usertype):
        self.decl.prefix = prefix

    def set_odintype(self, odintype: str, mode: str):
        self.decl.odintype = odintype
        self.mode          = mode


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

