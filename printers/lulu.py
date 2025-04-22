import gdb
import re
from typing import Final, Generator

def odin_parse_type(decl: str) -> str:
    """
    Assumptions:
    -   Odin does not append any length modifiers, or const/volatile qualifiers
        to pointer types.
    -   `struct ` (including the trailing space) is used to prefixed ALL
        composite types.
    """
    tag = decl.removeprefix("struct ")

    # Slice or fixed array?
    if tag[0] == '[':
        ...

def lookup_types(val: gdb.Value):
    utype = val.type.unqualified()

    # The following branch is an abomination
    try:
        tag = str(utype).removeprefix("struct ").strip()
        if tag[0] == '[':
            decl = tag[:tag.find(']')]
            size = 0
            for c in decl:
                if c.isdecimal():
                    size *= 10
                    size += int(c)

        if tag.find("[]") != -1:
            # Is a slice; will delegate back to here on a per-value basis
            return Odin_Slice(val, tag)

        try:
            # Is some other struct type; we need to look it up
            rawname = LULU_PATTERN.match(tag).group(1)
            return lulu_types_lookup[rawname](val)

        # AttributeError
        #   -   `.match()` returned `None` and we called `.group()`
        # KeyError
        #   -   `name` did not exist in the dict.
        except (AttributeError, KeyError):
            return odin_types_lookup[tag](val)
    except:
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


odin_types_lookup: Final = {
    "string": Odin_String,
}


###=== }}} =====================================================================


CONST_VOID_PTR: Final = gdb.lookup_type("void").const().pointer()

# Dissection:
#   lulu::
#       -   Ensure we find the prefix; The string literal `lulu::`.
#   (?:\[\w+\.odin\]::)?
#       -   `?:` indicates a non-capturing group.
#       -   *.odin filename assuming only alphabetical characters, enclosed in
#           square brackets and followed by delimiter `::`.
#   (\w+)
#       -   1st capturing group.
#       -   This is the raw name used for the struct, e.g. `Value` or `Table`.
# Links:
#   -   https://docs.python.org/3/library/re.html
LULU_PATTERN: Final  = re.compile(r'lulu::(?:\[\w+\.odin\]::)(\w+)')

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
lulu_types_lookup: Final = {
    "Value":     Lulu_Value,
    "OString":   Lulu_String,
    "OString *": Lulu_String,
}

