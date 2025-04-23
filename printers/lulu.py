import gdb
from typing import Final, Generator

# Ensure we can `import` the other 'modules' from this directory
import os
import sys
SCRIPT_PATH = os.path.dirname(os.path.realpath(__file__))
if SCRIPT_PATH not in sys.path:
    sys.path.append(SCRIPT_PATH)

import odin


demangler: Final = odin.Demangler()

# import traceback

def lookup_types(val: gdb.Value):
    try:
        utype  = val.type.unqualified()
        pretty = demangler.parse(str(utype))
        match pretty.length:
            case -2:
                if pretty.tag in type_printers:
                    return type_printers[pretty.tag](val)
            case -1: return Odin_Slice(val, pretty.tag)
            case _:  pass
    except:
        # Too noisy
        # traceback.print_exc()
        pass
    return None


gdb.pretty_printers.append(lookup_types)


###=== ODIN DATA TYPES ===================================================== {{{


class Odin_String:
    """
    struct string {
        u8 *data;
        int len;
    }
    """
    __data: str


    def __init__(self, val: gdb.Value):
        # `u8 *` can also be dereferenced properly as a string
        self.__data = val["data"].string(encoding="utf-8", length = int(val["len"]))


    def to_string(self) -> str:
        return self.__data


    def display_hint(self) -> str:
        return 'string'


class Odin_Slice:
    """
    struct []$T {
        T *data;
        int len;
    }
    """
    __tag:  str
    __data: gdb.Value
    __len:  int


    def __init__(self, val: gdb.Value, tag: str):
        self.__tag  = tag
        self.__data = val["data"] # Assumed to be a pointer of some kind
        self.__len  = int(val["len"])


    def children(self) -> tuple[str, gdb.Value]:
        return self.__next_element()


    def __next_element(self) -> Generator[tuple[str, gdb.Value], str, gdb.Value]:
        for i in range(self.__len):
            yield str(i), (self.__data + i).dereference()


    def to_string(self) -> str:
        """ Because of the `'array'` display hint, the actual data is printed
        by GDB using the `children()` method."""
        return f"{self.__tag}{{len = {self.__len}}}"


    def display_hint(self) -> str:
        return 'array'


###=== }}} =====================================================================


CONST_VOID_PTR: Final = gdb.lookup_type("void").const().pointer()

class Lulu_Value:
    """
    struct lulu::[value.odin]::Value {
        enum lulu::[value.odin]::Value_Type  type;
        union lulu::[value.odin]::Value_Data data;
    }
    """
    __pretty: str

    def __init__(self, val: gdb.Value):
        tag  = str(val["type"])
        data = val["data"]
        match tag:
            case "Nil":     self.__pretty = "nil"
            case "Boolean": self.__pretty = str(bool(data["boolean"]))
            case "Number":  self.__pretty = str(float(data["number"]))
            case "String":  self.__pretty = str(data["ostring"])
            case _:
                pointer = data["table"].cast(CONST_VOID_PTR)
                self.__pretty = f"{tag.lower()}: {pointer}"


    def to_string(self) -> str:
        return self.__pretty


class Lulu_Object_Header:
    """
    struct lulu::[object.odin]::Object_Header {
        enum lulu::[value.odin]::Value_Type        type;
        struct lulu::[object.odin]::Object_Header *prev;
    }
    """
    ...

class Lulu_String:
    """
    struct lulu::[string.odin]::OString {
        struct lulu::[object.odin]::Object_Header base;
        u32 hash;
        int len;
        u8  data[0];
    }
    """
    __data: str


    def __init__(self, val: gdb.Value):
        # Don't call `.address()`; that gives us `u8 (*)[0]`
        self.__data = val["data"].string(length = int(val["len"]))


    def to_string(self) -> str:
        return self.__data


    def display_hint(self) -> str:
        return 'string'


# Maybe easier to just hardcode the mangled names...
type_printers: Final = {
    "string":    Odin_String,
    "Value":     Lulu_Value,
    "OString":   Lulu_String,
    "OString *": Lulu_String, # All Odin pointers decay to C-style pointers
}

