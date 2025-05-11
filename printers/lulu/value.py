import gdb # type: ignore
from typing import Final
from .. import base

class ValuePrinter:
    """
    ```
    struct lulu::[value.odin]::Value {
        enum lulu::[value.odin]::Value_Type type;
        union lulu::[value.odin]::Value_Data data;
    }
    ```
    """
    __type: Final[str]
    __data: Final[gdb.Value]

    def __init__(self, val: gdb.Value):
        # In GDB, enums are already pretty-printed to their names
        self.__type = str(val["type"])
        self.__data = val["data"]

    def to_string(self) -> str:
        match self.__type:
            case "Nil":     return "nil"
            case "Boolean": return str(bool(self.__data["boolean"])).lower()
            case "Number":  return str(float(self.__data["number"]))
            # will (eventually) delegate to `odin.StringPrinter`
            case "String":  return str(self.__data["ostring"])

        # Assuming ALL data pointers have the same representation
        # Meaning `(void *)value.ostring == (void *)value.table` for the same
        # `lulu::Value` instance.
        pointer = self.__data["table"].cast(base.VOID_POINTER)
        return f"{self.__type.lower()}: {pointer}"

