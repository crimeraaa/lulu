import gdb # type: ignore
from typing import Final, Generator, TypeAlias, Optional, Literal

Iterator: TypeAlias = Generator[tuple[str, gdb.Value], str, None]

ENABLE_MSFT_WORKAROUNDS: Final = True
"""
The VSCode C/C++ extension, as well as Windows Subsystem for Linux (WSL),
seem to not properly respect the `map` display hint.
"""

VOID_POINTER:       Final = gdb.lookup_type("void").pointer()
CONST_CHAR_POINTER: Final = gdb.lookup_type("char").const().pointer()

def display_hint(hint: Literal["array", "map"]):
    return hint if ENABLE_MSFT_WORKAROUNDS else None
