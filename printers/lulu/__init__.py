# This is just for type annotations; the `gdb` module only exists within GDB!
import gdb # type: ignore
from typing import Final, Callable
from printers import odin

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
    __printers: Final[dict[str, Callable[[gdb.Value], str]]]

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
        super().__init__(name)

        # Assuming demangled but fully-qualified names
        self.__printers = {
            "lulu.Value":    ValuePrinter,
            "lulu.OString":  odin.StringPrinter,
            "^lulu.OString": odin.StringPrinter,
        }

    def __call__(self, val: gdb.Value):
        type_name = odin.pretty_printer.demangle(val)
        if type_name in self.__printers:
            return self.__printers[type_name](val)
        return None


VOID_PTR: Final = gdb.lookup_type("void").pointer()
UINTPTR:  Final = gdb.lookup_type("uintptr")


class ValuePrinter:
    """
    ```
    struct lulu::[value.odin]::Value {
        enum lulu::[value.odin]::Value_Type type;
        union lulu::[value.odin]::Value_Data data;
    }
    ```
    """
    __tag:  Final[str]
    __data: Final[gdb.Value]

    def __init__(self, val: gdb.Value):
        # In GDB, enums are already pretty-printed to their names
        self.__tag  = str(val["type"])
        self.__data = val["data"]

    def to_string(self) -> str:
        match self.__tag:
            # Python's builtin types
            case "Nil":     return "nil"
            case "Boolean": return str(bool(self.__data["boolean"]))
            case "Number":  return str(float(self.__data["number"]))
            # will (eventually) delegate to `lulu_OString`
            case "String":  return str(self.__data["ostring"])

        # Assuming ALL data pointers have the same representation
        # Meaning `(void *)value.ostring == (void *)value.table` for the same
        # `lulu::Value` instance.
        pointer = self.__data["table"].cast(VOID_PTR)
        return f"{self.__tag.lower()}: {pointer}"


pretty_printer = __PrettyPrinter("lulu_pretty_printer")
