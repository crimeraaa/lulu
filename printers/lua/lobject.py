import gdb # type: ignore
from enum import Enum
from typing import Final

VOID_POINTER:       Final = gdb.lookup_type("void").pointer()
CONST_CHAR_POINTER: Final = gdb.lookup_type("char").const().pointer()


class Type(Enum):
    NONE          = -1
    NIL           =  0
    BOOLEAN       =  1
    LIGHTUSERDATA =  2
    NUMBER        =  3
    STRING        =  4
    TABLE         =  5
    FUNCTION      =  6
    USERDATA      =  7
    THREAD        =  8


def bvalue(tvalue: gdb.Value) -> bool:
    return bool(tvalue["value"]["b"])


def nvalue(tvalue: gdb.Value) -> float:
    return float(tvalue["value"]["n"])


def pvalue(tvalue: gdb.Value) -> gdb.Value:
    return tvalue["value"]["p"]


def gcvalue(tvalue: gdb.Value) -> gdb.Value:
    return tvalue["value"]["gc"]


def svalue(tvalue: gdb.Value) -> str:
    # Let `TStringPrinter` handle it
    return str(gcvalue(tvalue)["ts"])


def getstr(tstring: gdb.Value) -> str:
    """
    **Parameters**
        `tstring` - represents a `TString *` (NOT a `TString`).

    **Analogous to**
    -   `#define getstr(ts) (const char *)(ts + 1)`

    **Assumptions**
    -   Addition should is overloaded for pointer types.
    -   `tstring` is the union with appropriate padding.
    -   `tstring.tsv` is the actual string data.
    -   For pointer arithmetic it is safer to use `ts`.

    **Guarantees**
    -   Embedded nul-characters `\0` are included.
    """
    data    = (tstring + 1).cast(CONST_CHAR_POINTER)
    nchars  = int(tstring["tsv"]["len"])

    # Only `char *` and variants thereof can safely use the `.string()` method.
    return data.string(length = nchars)


class TValuePrinter:
    """ NOTE(2025-04-20): Keep track of the field names in `lobject.h`! """
    __value: gdb.Value

    def __init__(self, value: gdb.Value):
        self.__value = value

    def to_string(self) -> str:
        # We assume `value.tt` is in range of `Type`
        tag = Type(int(self.__value["tt"]))
        match tag:
            case Type.NONE:    return "none"
            case Type.NIL:     return "nil"
            case Type.BOOLEAN: return str(bvalue(self.__value)).lower()
            case Type.NUMBER:  return str(nvalue(self.__value))
            case Type.STRING:  return svalue(self.__value)
            case _:
                # GDB already knows how to print addresses
                # assumes `(void *)TValue::value.p == (void *)TValue::value.gc`
                addr = gcvalue(self.__value).cast(VOID_POINTER)
                return f"{tag.name.lower()}: {str(addr)}"


class TStringPrinter:
    __ptr: gdb.Value

    def __init__(self, val: gdb.Value):
        # Get `&tsv` only if `val` is `TString` rather than `TString *`
        self.__ptr = val if val.type.code == gdb.TYPE_CODE_PTR else val.address

    def to_string(self) -> str:
        return getstr(self.__ptr)

    def display_hint(self):
        return "string"


class LocVarPrinter:
    __name:    gdb.Value
    __startpc: gdb.Value
    __endpc:   gdb.Value

    def __init__(self, val: gdb.Value):
        self.__name    = val["varname"]
        self.__startpc = val["startpc"]
        self.__endpc   = val["endpc"]

    def to_string(self) -> str:
        return f"{{{self.__name}: startpc={self.__startpc}, endpc={self.__endpc}}}"

