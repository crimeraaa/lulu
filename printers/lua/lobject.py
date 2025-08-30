from __future__ import annotations
import gdb # type: ignore
from enum import Enum
from .. import base

Value_Type = gdb.lookup_type("Value_Type")
VALUE_NONE = Value_Type["VALUE_NONE"].enumval
VALUE_NIL = Value_Type["VALUE_NIL"].enumval
VALUE_BOOLEAN = Value_Type["VALUE_BOOLEAN"].enumval
VALUE_LIGHTUSERDATA = Value_Type["VALUE_LIGHTUSERDATA"].enumval
VALUE_NUMBER = Value_Type["VALUE_NUMBER"].enumval
VALUE_STRING = Value_Type["VALUE_STRING"].enumval
VALUE_TABLE = Value_Type["VALUE_TABLE"].enumval
VALUE_FUNCTION = Value_Type["VALUE_FUNCTION"].enumval
VALUE_USERDATA = Value_Type["VALUE_USERDATA"].enumval
VALUE_THREAD = Value_Type["VALUE_THREAD"].enumval

def ttisnil(v: gdb.Value) -> bool:
    return int(v["tt"]) == VALUE_NIL


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


__tostring = {
    VALUE_NIL:     lambda _: "nil",
    VALUE_BOOLEAN: lambda v: str(bvalue(v)).lower(),
    VALUE_NUMBER:  lambda v: str(nvalue(v)),
    VALUE_STRING:  lambda v: svalue(v),
}

def tostring(v: gdb.Value) -> str:
    # We assume `value.tt` is in range of `Type`
    tag = v["tt"]
    if int(tag) in __tostring:
        return __tostring[int(tag)](v)

    # GDB already knows how to print addresses
    # assumes `(void *)TValue::value.p == (void *)TValue::value.gc`
    addr = gcvalue(v).cast(base.VOID_POINTER)
    t = str(tag).removeprefix("VALUE_").lower()
    return f"{t}: {addr}"


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

