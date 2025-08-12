import gdb # type: ignore
from .. import base
from typing import Callable

Value_Type = gdb.lookup_type("Value_Type")

# Can't compare `gdb.Value` to `gdb.Field`, get enum value instead
VALUE_LIGHTUSERDATA = Value_Type["VALUE_LIGHTUSERDATA"].enumval

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


    __TOSTRING: dict[str, Callable[[gdb.Value], str]] = {
        "nil":      lambda _: "nil",
        "boolean":  lambda data: str(bool(data["m_boolean"])),
        "number":   lambda data: str(float(data["m_number"])),
        "string":   lambda data: str(data["m_object"]["ostring"].address),
        "integer":  lambda data: str(int(data["m_integer"])),
    }

    def __init__(self, val: gdb.Value):
        # In GDB, enums are already pretty-printed to their names
        self.__type = val["m_type"]
        # No *named* union to access
        self.__data = val

    def to_string(self) -> str:
        t = str(self.__type).removeprefix("VALUE_").lower()
        if t in self.__TOSTRING:
            return self.__TOSTRING[t](self.__data)

        if self.__type == VALUE_LIGHTUSERDATA:
            p = self.__data["m_pointer"]
        else:
            p = self.__data["m_object"].cast(base.VOID_POINTER)
        return f"{t}: {p}"


