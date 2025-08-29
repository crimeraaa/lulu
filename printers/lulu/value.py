import gdb # type: ignore
from .. import base
from typing import Callable

Value_Type = gdb.lookup_type("Value_Type")

# Can't compare `gdb.Value` to `gdb.Field`, get enum value instead
VALUE_STRING = Value_Type["VALUE_STRING"].enumval
VALUE_FUNCTION = Value_Type["VALUE_FUNCTION"].enumval

def ensure_pointer(v: gdb.Value):
    return v if v.type.code == gdb.TYPE_CODE_PTR else v.address


class OString_Printer:
    __value: gdb.Value

    def __init__(self, v: gdb.Value):
        self.__value = ensure_pointer(v)

    def to_string(self) -> str:
        # Can't call `.string()` on `char[1]`
        s = self.__value["data"].cast(base.CONST_CHAR_POINTER)
        n = int(self.__value["len"])
        return s.string(length = n)

    def display_hint(self) -> str:
        return "string"


class Object_Printer:
    __value: gdb.Value

    def __init__(self, v: gdb.Value):
        self.__value = ensure_pointer(v)

    def to_string(self):
        v = self.__value
        if not v:
            return "(null)"
        t = v["base"]["type"]
        if t == VALUE_STRING:
            return v["ostring"].address

        t = str(t).removeprefix("VALUE_").lower()
        p = v.cast(base.VOID_POINTER)
        return f"{t}: {p}"


# In:  Object *
# Out: OString * | Table * | Chunk * | Closure * | Upvalue * | None
def object_get_data(node: gdb.Value):
    if not node:
        return None

    # Don't call the type() method; may crash
    t = node["base"]["type"]
    if t == VALUE_STRING:
        return node["ostring"].address
    else:
        s = object_get_type_name(node)
        # p.type, is p is a pointer, returns None, annoyingly enough
        p = node[s].address
        if t == VALUE_FUNCTION:
            kind = 'c' if p["c"]["is_c"] else "lua"
            p = p[kind].address
        return p


def object_get_type_name(node: gdb.Value):
    if not node:
        return "None"
    t = str(node["base"]["type"])
    return t.removeprefix("VALUE_").lower()


def object_iterator(node: gdb.Value, field = "next"):
    i = 0
    data = object_get_data(node)
    while node:
        yield str(i), data

        i += 1
        # field is "gc_list", this is assumed to be safe because only
        # objects with this member get linked into the gray list.
        # Otherwise it's "next" which is always safe.
        node = data[field]
        data = object_get_data(node)


class Object_List_Printer:
    __value: gdb.Value

    def __init__(self, v: gdb.Value):
        self.__value = ensure_pointer(v)

    def children(self):
        node = self.__value
        return object_iterator(node)

    def display_hint(self):
        return "array"


class GC_List_Printer:
    __value: gdb.Value

    def __init__(self, v: gdb.Value):
        self.__value = ensure_pointer(v)

    def children(self):
        node = self.__value
        return object_iterator(node, "gc_list")

    def display_hint(self):
        return "array"


class Value_Printer:
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


