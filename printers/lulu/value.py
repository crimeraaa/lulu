import gdb # type: ignore
from typing import Final
from .. import base

class ValuePrinter:
    """
    ```
    struct Value {
        Value_Type type;
        union {...};
    }
    ```
    """
    __type: Final[str]
    __data: Final[gdb.Value]

    def __init__(self, val: gdb.Value):
        # In GDB, enums are already pretty-printed to their names
        self.__type = str(val["type"])
        # No *named* union to access
        self.__data = val

    def to_string(self) -> str:
        match self.__type:
            case "LULU_TYPE_NIL":     return "nil"
            case "LULU_TYPE_BOOLEAN": return str(bool(self.__data["boolean"]))
            case "LULU_TYPE_NUMBER":  return str(float(self.__data["number"]))
            case _:
                pass

        return "unknown"

