import gdb # type: ignore
from .. import base
from typing import Callable

VALUE_NONE      = -1
VALUE_NIL       = 0
VALUE_BOOLEAN   = 1
VALUE_NUMBER    = 2
VALUE_USERDATA  = 3
VALUE_STRING    = 4
VALUE_TABLE     = 5
VALUE_FUNCTION  = 6
VALUE_CHUNK     = 7

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
    

    __TOSTRING: dict[int, Callable[[gdb.Value], str]] = {
        VALUE_NONE:     lambda _: "none",
        VALUE_NIL:      lambda _: "nil",
        VALUE_BOOLEAN:  lambda data: str(bool(data["m_boolean"])),
        VALUE_NUMBER:   lambda data: str(float(data["m_number"])),
        VALUE_STRING:   lambda data: str(data["m_object"]["ostring"].address),
    }

    def __init__(self, val: gdb.Value):
        # In GDB, enums are already pretty-printed to their names
        self.__type = val["m_type"]
        # No *named* union to access
        self.__data = val

    def to_string(self) -> str:
        tv = int(self.__type)
        if tv in self.__TOSTRING:
            return self.__TOSTRING[tv](self.__data)

        t = str(self.__type).removeprefix("VALUE_").lower()
        if self.__type == VALUE_USERDATA:
            p = self.__data["m_pointer"]
        else:
            p = self.__data["m_object"].cast(base.VOID_POINTER)
        return f"{t}: {p}"


