import gdb # type: ignore
from typing import Final, Generator, Literal, Type, TypeAlias, Optional

Iterator: TypeAlias = Generator[tuple[str, gdb.Value], str, None]

ENABLE_MSFT_WORKAROUNDS: Final = True
"""
The VSCode C/C++ extension, as well as Windows Subsystem for Linux (WSL),
seem to not properly respect the `map` display hint.
"""

VOID_POINTER:       Final = gdb.lookup_type("void").pointer()
CONST_CHAR_POINTER: Final = gdb.lookup_type("char").const().pointer()

Display_Hint: TypeAlias = Literal["string", "array", "map"]

def display_hint(hint: Display_Hint) -> Optional[Display_Hint]:
    return hint if ENABLE_MSFT_WORKAROUNDS else None

def subprinters(*names: str):
    return [gdb.printing.SubPrettyPrinter(name) for name in names]
