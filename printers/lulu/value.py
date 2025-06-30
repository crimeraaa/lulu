import gdb # type: ignore
from .. import base

class OStringPrinter:
    __value: gdb.Value

    def __init__(self, v: gdb.Value):
        if v.type.code != gdb.TYPE_CODE_PTR:
            v = v.address
        self.__value = v

    def to_string(self) -> str:
        # Can't call `.string()` on `char[1]`
        s = self.__value["data"].cast(base.CONST_CHAR_POINTER)
        n = int(self.__value["len"])
        return s.string(length = n)

    def display_hint(self) -> str:
        return "string"


class ValuePrinter:
    """
    ```
    struct Value {
        Value_Type type;
        union {...};
    }
    ```
    """
    __type: gdb.Value
    __data: gdb.Value

    def __init__(self, val: gdb.Value):
        # In GDB, enums are already pretty-printed to their names
        self.__type = val["type"]
        # No *named* union to access
        self.__data = val

    def to_string(self) -> str:
        match int(self.__type):
            case -1: return "none"
            case 0:  return "nil"
            case 1:  return str(bool(self.__data["boolean"]))
            case 2:  return str(float(self.__data["number"]))
            case 4:  return str(self.__data["object"]["ostring"].address)
            case _:
                pass


        t = str(self.__type).removeprefix("VALUE_").lower()
        p = self.__data["object"].cast(base.VOID_POINTER)
        return f"{t}: {p}"


