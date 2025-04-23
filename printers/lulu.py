import gdb
from typing import Final, Generator, Optional

# Ensure we can `import` the other 'modules' from this directory
import os
import sys

SCRIPT_PATH = os.path.dirname(os.path.realpath(__file__))
if SCRIPT_PATH not in sys.path:
    sys.path.append(SCRIPT_PATH)
    sys.path.append(SCRIPT_PATH + "/odin")

import odin.demangler


demangler: Final = odin.demangler.Parser()

# import traceback

def lookup_types(val: gdb.Value):
    try:
        utype = str(val.type.unqualified())
        demangled = odin.demangler.parse(demangler, utype)
        # print(demangled)

        tag = demangled.tag
        match demangled.kind:
            case "slice":
                return Odin_Slice(val, tag)
            case "dynamic":
                return Odin_Slice(val, tag, True)
            case _:
                pass

        if tag in type_printers:
            return type_printers[tag](val)
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
    __cap:  Optional[int]


    def __init__(self, val: gdb.Value, tag: str, has_cap = False):
        self.__tag  = tag
        self.__data = val["data"] # Assumed to be a pointer of some kind
        self.__len  = int(val["len"])
        self.__cap  = int(val["cap"]) if has_cap else None


    def children(self) -> tuple[str, gdb.Value]:
        return self.__iter__()


    def __iter__(self) -> Generator[tuple[str, gdb.Value], str, gdb.Value]:
        for i in range(self.__len):
            yield str(i), (self.__data + i).dereference()


    def to_string(self) -> str:
        """ Because of the `'array'` display hint, the actual data is printed
        by GDB using the `children()` method. """
        info = f"len = {self.__len}"
        if self.__cap is not None:
            info += f", cap = {self.__cap}"
        return f"{self.__tag}{{{info}}}"


    def display_hint(self) -> str:
        return 'array'


UINTPTR: Final = gdb.lookup_type("uintptr")


class Odin_Map:
    """
    Links:
    - https://pkg.odin-lang.org/base/runtime/#Raw_Map

    Odin: ```map[string]^OString```

    C:
    ```

    // `struct{...}` is the very compressed Odin-style struct definition used as
    // the mangled name.
    #define ELEM_TYPENAME  struct{\
        key:string,\
        value:^lulu::[string.odin]::OString,\
        hash:uintptr,\
        key_cell:string,\
        value_cell:^lulu::[string.odin]::OString\
    }

    struct map[string]^lulu::[string.odin]::OString {
        struct ELEM_TYPENAME *data;
        int len;
        struct runtime::Allocator allocator;
    }

    struct ELEM_TYPENAME {
        struct string key;
        struct lulu::[string.odin]::OString *value;
        uintptr hash;
        struct string key_cell;
        struct lulu::[string.odin]::OString *value_cell;
    }
    ```
    """
    __tag:  str
    __data: gdb.Value
    __len:  int
    __cap:  int


    # The lower 6 bits of `__data` are used as log2 of the actual capacity.
    MASK_CAP: Final = 0b0011_1111


    def __init__(self, value: gdb.Value, tag: str):
        data = value["data"].cast(UINTPTR)
        self.__tag  = tag
        self.__data = data
        self.__len  = value["len"]
        if data == 0:
            self.__cap = 0
        else:
            self.__cap = 1 << int(self.__log2_cap(self))


    def __log2_cap(self) -> int:
        """
        Links:
        -   https://github.com/odin-lang/Odin/blob/master/base/runtime/dynamic_map_internal.odin#L192
        """
        return int(self.__data) & self.MASK_CAP


    def __get_data(self) -> gdb.Value:
        return self.__data & ~self.MASK_CAP


    def children(self) -> tuple[str, gdb.Value]:
        return self.__iter__()


    def __iter__(self) -> Generator[tuple[str, gdb.Value], str, gdb.Value]:
        for i in range(self.__len):
            # TODO(2025-04-24): Due to how `map` is implemented in Odin, this
            # mere index operation is not enough!
            yield str(i), (self.__data + i).dereference()


    def to_string(self) -> str:
        return f"{self.__tag}{{len = {self.__len}, cap = {self.__cap}}}"


    def display_hint(self) -> str:
        return 'array'


###=== }}} =====================================================================


CONST_VOID_PTR: Final = gdb.lookup_type("void").const().pointer()
NULL:           Final = gdb.Value(0).cast(CONST_VOID_PTR)

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

    # All Odin pointers decay to C-style pointers
    "OString *": Lulu_String,
}

