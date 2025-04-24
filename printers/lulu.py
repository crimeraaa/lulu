import gdb
from typing import Final, Generator, Optional

import os
import sys

SCRIPT_PATH = os.path.dirname(os.path.realpath(__file__))
if SCRIPT_PATH not in sys.path:
    sys.path.append(SCRIPT_PATH) # `import` from `../printers/`
    sys.path.append(SCRIPT_PATH + "/odin") # `import` from `../printers/odin/`


import odin.demangler


def lookup_types(val: gdb.Value):
    try:
        utype = str(val.type.unqualified())
        if utype in type_printers:
            return type_printers[utype](val)

        demangled, tag = odin.demangler.demangle(utype)

        match demangled.mode:
            case "array":
                """
                Likely impossible; Odin arrays just 100% C declarations so our
                demangler cannot (and *will* not) handle these cases.
                -   `[256]byte` becomes `byte [256]`.
                -   `[2][3]f32` becomes `f32 [2][3]`.
                -   `[16]^byte` becomes `byte *[16]`.
                -   `[8]^string` becomes `struct string *[8]`.

                Pointers to any of the above get very ugly very fast due to how
                C's type declarations work. E.g:
                -   `^[256]byte` becomes `byte (*)[256]`
                -   `^[2][3]f32` becomes `f32 (*)[2][3]`
                -   `^[16]^byte` becomes `byte *(*)[16]`
                -   `^[8]^string` becomes `struct string *(*)[8]`

                Want even more pain? Try arrays-of-pointers-to-arrays!
                -   `[2]^[3]f32` becomes `byte (*[2])[3]`
                -   `^[2]^[3]f32` becomes `byte (*(*)[2])[3]`

                Fortunately for us, GDB already knows how to deal with fixed-size
                arrays (and pointers thereof) so we don't need to create our own
                special logic.
                """
                pass
            case "slice":
                return odin_Slice(val, tag)
            case "dynamic":
                return odin_Slice(val, tag, has_cap=True)
            case "map":
                # return Odin_Map(val, pattern[1] + pattern[2] + pattern[3])
                pass
    except:
        pass
    return None


gdb.pretty_printers.append(lookup_types)



###=== ODIN DATA TYPES ===================================================== {{{


class odin_String:
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


class odin_Slice:
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
        info = f"len={self.__len}"
        if self.__cap is not None:
            info += f", cap={self.__cap}"
        return f"{self.__tag}{{{info}}}"


    def display_hint(self) -> str:
        return 'array'


UINTPTR: Final = gdb.lookup_type("uintptr")


class odin_Map:
    """
    Links:
    - https://pkg.odin-lang.org/base/runtime/#Raw_Map

    Odin: ```map[$K]$V```

    C:
    ```

    // `struct{...}` is the very compressed Odin-style struct definition used as
    // the struct tag.
    #define DATA_TYPE_TAG \
        struct{key:$K,value:$V,hash:uintptr,key_cell:$K,value_cell:$V}

    struct map[$K]$V {
        struct DATA_TYPE_TAG *data;
        int len;
        struct runtime::Allocator allocator;
    }

    struct DATA_TYPE_TAG {
        $K key;
        $V value;
        uintptr hash;
        $K key_cell;
        $V value_cell;
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

class lulu_Value:
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


class lulu_Object_Header:
    """
    struct lulu::[object.odin]::Object_Header {
        enum lulu::[value.odin]::Value_Type        type;
        struct lulu::[object.odin]::Object_Header *prev;
    }
    """
    ...

class lulu_OString:
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
    "struct string":                    odin_String,
    "struct lulu::[value.odin]::Value": lulu_Value,
    "struct OString":                   lulu_OString,

    # All Odin pointers decay to C-style pointers
    "struct lulu::[string.odin]::OString *": lulu_OString,
}

