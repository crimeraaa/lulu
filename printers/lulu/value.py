import gdb # type: ignore
from .. import base
from typing import Callable

Value_Type = gdb.lookup_type("Value_Type")

# Can't compare `gdb.Value` to `gdb.Field`, get enum value instead
VALUE_STRING = Value_Type["VALUE_STRING"].enumval
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

class Simple_Object_Printer:
    __value: gdb.Value

    def __init__(self, v: gdb.Value):
        if v.type.code != gdb.TYPE_CODE_PTR:
            v = v.address
        self.__value = v

    def to_string(self):
        t = str(self.__value["type"]).removeprefix("VALUE_").lower()
        p = self.__value.cast(base.VOID_POINTER)
        return f"{t}: {p}"

class Closure_Printer:
    __value: gdb.Value

    def __init__(self, v: gdb.Value):
        if v.type.code != gdb.TYPE_CODE_PTR:
            v = v.address
        self.__value = v

    def to_string(self):
        p = self.__value.cast(base.VOID_POINTER)
        return f"function: {p}"


class Object_Printer:
    __value: gdb.Value

    def __init__(self, v: gdb.Value):
        self.__value = v

    def children(self):
        node = self.__value
        i = 0
        while node:
            t = node["base"]["type"]
            if t == VALUE_STRING:
                payload = node["ostring"].address
            else:
                t = str(t).removeprefix("VALUE_").lower()
                payload = node[t].address

            yield str(i), payload

            i += 1
            node = node["base"]["next"]

    def display_hint(self):
        return "array"

    def to_string(self) -> str:
        if self.__value == 0:
            return "(null)"
        return f"({str(self.__value.type)})"


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
        "boolean":  lambda v: str(bool(v["m_boolean"])),
        "number":   lambda v: str(float(v["m_number"])),
        "string":   lambda v: str(v["m_object"]["ostring"].address),
        "integer":  lambda v: str(int(v["m_integer"])),
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

        # Assumes value.m_pointer == (void *)value.m_object
        p = self.__data["m_pointer"]
        return f"{t}: {p}"


