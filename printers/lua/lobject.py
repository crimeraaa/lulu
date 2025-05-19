from __future__ import annotations
import gdb # type: ignore
from enum import Enum
from .. import base

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

def ttisnil(v: gdb.Value) -> bool:
    return int(v["tt"]) == Type.NIL.value

def bvalue(v: gdb.Value) -> bool:
    return bool(v["value"]["b"])


def nvalue(v: gdb.Value) -> float:
    return float(v["value"]["n"])


def pvalue(v: gdb.Value) -> gdb.Value:
    return v["value"]["p"]


def gcvalue(v: gdb.Value) -> gdb.Value:
    return v["value"]["gc"]


def svalue(v: gdb.Value) -> str:
    # Let `TStringPrinter` handle it
    return str(gcvalue(v)["ts"].address)

def hvalue(v: gdb.Value) -> gdb.Value:
    return gcvalue(v)['h'].address


def getstr(ts: gdb.Value) -> str:
    """
    **Parameters**
        `ts` - represents a `TString *` (NOT a `TString`).

    **Analogous to**
    -   `#define getstr(ts) (const char *)(ts + 1)`

    **Assumptions**
    -   Addition should is overloaded for pointer types.
    -   `ts` is the union with appropriate padding.
    -   `ts.tsv` is the actual string data.
    -   For pointer arithmetic it is safer to use `ts`.

    **Guarantees**
    -   Embedded nul-characters `\0` are included.
    """
    data    = (ts + 1).cast(base.CONST_CHAR_POINTER)
    nchars  = int(ts["tsv"]["len"])

    # Only `char *` and variants thereof can safely use the `.string()` method.
    return data.string(length = nchars)


class TValuePrinter:
    """ NOTE(2025-04-20): Keep track of the field names in `lobject.h`! """
    __value: gdb.Value

    def __init__(self, v: gdb.Value):
        self.__value = v

    def to_string(self) -> str:
        return tostring(self.__value)


def tostring(v: gdb.Value) -> str:
    # We assume `value.tt` is in range of `Type`
    tag = Type(int(v["tt"]))
    match tag:
        case Type.NONE:    return "none"
        case Type.NIL:     return "nil"
        case Type.BOOLEAN: return str(bvalue(v)).lower()
        case Type.NUMBER:  return str(nvalue(v))
        case Type.STRING:  return svalue(v)
        case _:
            pass
    # GDB already knows how to print addresses
    # assumes `(void *)TValue::value.p == (void *)TValue::value.gc`
    addr = gcvalue(v).cast(base.VOID_POINTER)
    return f"{tag.name.lower()}: {addr}"


first_table = True
visited: dict[gdb.Value, bool] = {}


class TStringPrinter:
    __ptr: gdb.Value

    def __init__(self, val: gdb.Value):
        # Get `&tsv` only if `val` is `TString` rather than `TString *`
        self.__ptr = val if val.type.code == gdb.TYPE_CODE_PTR else val.address

    def to_string(self) -> str:
        return getstr(self.__ptr)

    def display_hint(self):
        return "string"


class NodePrinter:
    __val: gdb.Value
    __key: gdb.Value

    def __init__(self, v: gdb.Value):
        self.__val = v["i_val"]
        self.__key = v["i_key"]["tvk"]

    def to_string(self) -> str:
        return f"[{str(self.__key)}]={self.__val}"


class LocVarPrinter:
    __name:    gdb.Value
    __startpc: gdb.Value
    __endpc:   gdb.Value

    def __init__(self, v: gdb.Value):
        self.__name    = v["varname"]
        self.__startpc = v["startpc"]
        self.__endpc   = v["endpc"]

    def to_string(self) -> str:
        return f"{{{self.__name}: startpc={self.__startpc}, endpc={self.__endpc}}}"

