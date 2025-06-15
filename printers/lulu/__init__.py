# This is just for type annotations; the `gdb` module only exists within GDB!
import gdb # type: ignore
from typing import Final
from printers import odin, base
from . import opcode, lexer, expr, value

class __PrettyPrinter(gdb.printing.PrettyPrinter):
    """
    Usage:
    -   Create `lulu/bin/lulu-gdb.py` right next to `lulu/bin/lulu`.
    -   When invoking `gdb lulu/bin/lulu`, it will load `lulu/bin/lulu-gdb.py`
        if auto loading of scripts was enabled.

    Notes:
    -   GDB Python does NOT include the relative current directory in `sys.path`.
    -   We handle this by manually adding the lulu repo's directory to `sys.path`
        within `.gdbinit`.

    Sample:
    ```
    # lulu/bin/lulu-gdb.py
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
            "Expr":         expr.ExprPrinter,
            "Value":        value.ValuePrinter,
            "String":       odin.StringPrinter,

            # Pointers thereof
            "Instruction *": opcode.InstructionPrinter,
        }
        super().__init__(name, subprinters=base.subprinters(*list(self.__printers)))


    def __call__(self, val: gdb.Value):
        # Seems that from within the VSCode debugger an exception is silently
        # raised and squashed while trying to print `lulu.VM`
        #
        # Curiously, running GDB on the command line doesn't show any errors.
        try:
            tag, _ = odin.pretty_printer.demangle(val)
            return self.__printers[tag](val)
        except:
            pass
        return None


pretty_printer = __PrettyPrinter("lulu")
