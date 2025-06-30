# This is just for type annotations; the `gdb` module only exists within GDB!
import gdb # type: ignore
from typing import Final
from printers import odin, base
from . import opcode, lexer, expr, value

from typing import cast

class __PrettyPrinter(gdb.printing.PrettyPrinter):
    """
    Usage:
    -   If lulu and its implementation was compiled into a standalone executable
        then create `lulu/bin/lulu-gdb.py` right next to `lulu/bin/lulu`.
    -   When invoking `gdb lulu/bin/lulu`, it will load `lulu/bin/lulu-gdb.py`
        if auto loading of scripts was enabled.

    -   Otherwise, if lulu was first compiled as a shared library, e.g.
        `lulu/bin/liblulu.so`, then create `lulu/bin/liblulu.so-gdb.py` instead.
    -   When `bin/liblulu.so` (NOT the main executable) is loaded into memory,
        GDB will load the Python script.

    Notes:
    -   GDB Python does NOT include the relative current directory in `sys.path`.
    -   We handle this by manually adding the lulu repo's directory to `sys.path`
        within `.gdbinit`.

    Sample:
    ```
    # lulu/bin/lulu-gdb.py or lulu/bin/liblulu.so-gdb.py
    import gdb
    from printers import odin, lulu

    inferior = gdb.current_objfile()
    gdb.printing.register_pretty_printer(inferior, lulu.pretty_printer)
    gdb.printing.register_pretty_printer(inferior, odin.pretty_printer)
    # Make sure to clean up the global namespace!
    del inferior
    ```
    """
    __printers: Final

    def __init__(self, name: str):
        """
        NOTE(2025-04-26):
        -   I'd love to use `gdb.printing.RegexpCollectionPrettyPrinter()`.
        -   However, based on the `__call__` implementation (e.g. in
            `/usr/share/gdb/python/gdb/printing.py`) it decares
            `typename = gdb.types.get_basic_type(val.type).tag`.
        -   This is bad for us, because pointers *don't* have tags!
        -   If that was `None`, it tries `typename = val.type.name`.
        -   That also doesn't work, because again, pointers don't have
            such information on their own.
        -   So even if our regex allows for pointers, the parent class
            literally does not allow you to work with pointers.
        """
        # Assuming demangled but fully-qualified names
        self.__printers = {
            # Structs
            "Instruction":  opcode.InstructionPrinter,
            "Token":        lexer.TokenPrinter,
            "Line_Info":    lexer.LineInfoPrinter,
            "Expr":         expr.ExprPrinter,
            "Value":        value.ValuePrinter,
            "LString":      odin.StringPrinter,
            "OString":      value.OStringPrinter,

            # Pointers thereof
            # "Instruction *": opcode.InstructionPrinter,
            "Token *":      lexer.TokenPrinter,
            "Expr *":       expr.ExprPrinter,
            "OString *":    value.OStringPrinter,
        }
        super().__init__(name, subprinters=base.subprinters(*list(self.__printers)))

    def __resolve_typename(self, v: gdb.Value) -> str:
        # The `.type` field for pointers doesn't have a `.name` field.
        # https://sourceware.org/gdb/current/onlinedocs/gdb.html/Types-In-Python.html#Types-In-Python
        match v.type.code:
            case gdb.TYPE_CODE_PTR:
                tag = v.type.target().name
                # e.g. `Object **`
                if tag is None:
                    return str(v.type)
                return tag + " *"
            case gdb.TYPE_CODE_REF:
                # e.g. `lulu_VM &` is a reference to `lulu_VM`,
                # `Object *&` is a reference to `Object *`.
                return self.__resolve_typename(v.referenced_value())
            case gdb.TYPE_CODE_ARRAY:
                return str(v.type)
            case _:
                # Only structs, unions and enums can have tags, so use `name`.
                return v.type.name

    def __call__(self, v: gdb.Value):
        tag = self.__resolve_typename(v)
        if not tag:
            raise ValueError(f"{v.type} could not be resolved to a string")
        
        # TODO(2025-06-27): Differentiate from dependent typenames, e.g.
        # Slice<Value>::pointer
        if tag.startswith("Slice<") and tag.endswith(">"):
            return odin.SlicePrinter(v, tag)
        elif tag.startswith("Dynamic<") and tag.endswith(">"):
            return odin.DynamicPrinter(v, tag)
        elif tag in self.__printers:
            return self.__printers[tag](v)
        return None


pretty_printer = __PrettyPrinter("lulu")
