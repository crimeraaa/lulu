import gdb  # Need to configure .vscode/settings.json for this to show in PyLance!
from enum import Enum
from typing import Final, Union, TypeAlias


def lua_pretty_printers_lookup(val: gdb.Value):
    # Strip away `const` and/or `volatile`
    utype = val.type.unqualified()

    # Strip away pointers until we hit the most pointed-to type.
    while utype.code == gdb.TYPE_CODE_PTR:
        # Get the pointed-to `gdb.Type`, e.g. `TValue` from `TValue *`.
        # This also works for `void *`; We end up with `void`.
        utype = utype.target().unqualified()

        # We need to update `val` so that all our printers assume they work with
        # values/structs as-is, not pointers thereof.
        if utype.code != gdb.TYPE_CODE_VOID:
            val = val.dereference()

    if utype.name in lua_pretty_printers:
        return lua_pretty_printers[utype.name](val)
    return None


gdb.pretty_printers.append(lua_pretty_printers_lookup)


###=== lopcodes.h ========================================================== {{{


SIZE_B:     Final =  9
SIZE_C:     Final =  SIZE_B
SIZE_Bx:    Final =  SIZE_B + SIZE_C
SIZE_A:     Final =  8
SIZE_OP:    Final =  6


MASK_B:     Final =  (1 << SIZE_B)  - 1 # 0b00000000_00000000_00000001_11111111
MASK_C:     Final =  MASK_B
MASK_Bx:    Final =  (1 << SIZE_Bx) - 1 # 0b00000000_00000011_11111111_11111111
MASK_A:     Final =  (1 << SIZE_A)  - 1 # 0b00000000_00000000_00000000_11111111
MASK_OP:    Final =  (1 << SIZE_OP) - 1 # 0b00000000_00000000_00000000_00111111


OFFSET_OP:  Final =  0
OFFSET_A:   Final =  OFFSET_OP + SIZE_OP
OFFSET_C:   Final =  OFFSET_A  + SIZE_A
OFFSET_B:   Final =  OFFSET_C  + SIZE_C
OFFSET_Bx:  Final =  OFFSET_C


MAXARG_Bx:  Final = (1 << SIZE_Bx) - 1
MAXARG_sBx: Final =  MAXARG_Bx >> 1


BITRK: Final = 1 << (SIZE_B - 1) # 0b1_0000_0000


OpCode: Final = gdb.lookup_type("OpCode")


class OpMode(Enum):
    iABC  = 1
    iABx  = 2
    iAsBx = 3


# Special cases; assume all other instructions are in the form iABC
luaP_opmodes: dict[str, OpMode] = {
    "OP_LOADK":     OpMode.iABx,
    "OP_GETGLOBAL": OpMode.iABx,
    "OP_SETGLOBAL": OpMode.iABx,
    "OP_JMP":       OpMode.iAsBx,
    "OP_TFORLOOP":  OpMode.iAsBx,
    "OP_CLOSURE":   OpMode.iABx,
}


def GET_OPCODE(i: int) -> int:
    return ((i >> OFFSET_OP) & MASK_OP)


def GETARG_A(i: int) -> int:
    return (i >> OFFSET_A) & MASK_A


def GETARG_B(i: int) -> int:
    return (i >> OFFSET_B) & MASK_B


def GETARG_C(i: int) -> int:
    return (i >> OFFSET_C) & MASK_C


def GETARG_Bx(i: int) -> int:
    return (i >> OFFSET_Bx) & MASK_Bx


def GETARG_sBx(i: int) -> int:
    return GETARG_Bx(i) - MAXARG_sBx


def ISK(reg: int) -> bool:
    """ Tests if `reg` represents a constant index rather than a register. """
    return (reg & BITRK) != 0


def INDEXK(reg: int) -> int:
    # Don't use bitwise not `~` because of how Python's big integers work
    return reg & (BITRK - 1)


class InstructionPrinter:
    __ip:     int
    __pretty: str


    def __init__(self, val: gdb.Value):
        # `Instruction` is just a typedef for some unsigned 32-bit integer
        self.__ip = int(val)
        # GDB already represents enums as their names, not their values
        op   = str(gdb.Value(GET_OPCODE(self.__ip)).cast(OpCode))
        a    = GETARG_A(self.__ip)
        args = [f"{op.removeprefix('OP_')}: A={a}, "]
        if op in luaP_opmodes:
            # The only ones we mapped are iABx or iAsBx
            if luaP_opmodes[op] == OpMode.iABx:
                bx = GETARG_Bx(self.__ip)
                args.append(f"Bx={bx}")
            else:
                sbx = GETARG_sBx(self.__ip)
                args.append(f"sBx={sbx}")
        else:
            b = GETARG_B(self.__ip)
            c = GETARG_C(self.__ip)
            args.append(f"B={b}, C={c}")
        self.__pretty = "".join(args)


    def to_string(self) -> str:
        return self.__pretty


###=== }}} =====================================================================


###=== lobject.h =========================================================== {{{


VOID_POINTER:       Final = gdb.lookup_type("void").pointer()
CONST_CHAR_POINTER: Final = gdb.lookup_type("char").const().pointer()


class lua_Type(Enum):
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


luaT_typenames: dict[lua_Type, str] = {
    lua_Type.NONE:          "none",
    lua_Type.NIL:           "nil",
    lua_Type.BOOLEAN:       "boolean",
    lua_Type.LIGHTUSERDATA: "lightuserdata",
    lua_Type.NUMBER:        "number",
    lua_Type.STRING:        "string",
    lua_Type.TABLE:         "table",
    lua_Type.FUNCTION:      "function",
    lua_Type.USERDATA:      "userdata",
    lua_Type.THREAD:        "thread",
}


# `lobject.h:union Value`
Value: TypeAlias = Union[None | bool | int | str | gdb.Value]


def bvalue(tvalue: gdb.Value) -> bool:
    return tvalue["value"]["b"]


def nvalue(tvalue: gdb.Value) -> float:
    return tvalue["value"]["n"]


def pvalue(tvalue: gdb.Value) -> gdb.Value:
    return tvalue["value"]["p"]


def gcvalue(tvalue: gdb.Value) -> gdb.Value:
    return tvalue["value"]["gc"]


def svalue(tvalue: gdb.Value) -> str:
    """
    **Analogous to**
    -   `&gcvalue(tvalue)->ts`

    **Notes**
    -   `TValue::value.gc->ts` itself is a `TString` structure, *not* a pointer.
    -   We need to explicitly get the address in order to actually do pointer
        arithmetic in `getstr()`.
    """
    return getstr(gcvalue(tvalue)["ts"].address)


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
    -   The resulting string is surrounded by double quotes.
    """
    data    = (tstring + 1).cast(CONST_CHAR_POINTER)
    nchars  = tstring["tsv"]["len"]
    quote   = '\'' if nchars == 1 else '\"'

    # Only `char *` and variants thereof can safely use the `.string()` method.
    return "".join([quote, data.string(length = nchars), quote])


class TValuePrinter:
    """ NOTE(2025-04-20): Keep track of the field names in `lobject.h`! """
    __value: Value
    __tag:   lua_Type


    def __init__(self, value: gdb.Value):
        # We assume `value.tt` is in range of `lua_Type`
        tag = lua_Type(int(value["tt"]))
        actual: Value
        match tag:
            case lua_Type.NIL:           actual = None
            case lua_Type.NONE:          actual = None
            case lua_Type.BOOLEAN:       actual = bvalue(value)
            case lua_Type.LIGHTUSERDATA: actual = pvalue(value)
            case lua_Type.NUMBER:        actual = nvalue(value)
            case lua_Type.STRING:        actual = svalue(value)
            case _:
                actual = gcvalue(value).cast(VOID_POINTER)
        self.__value = actual
        self.__tag   = tag


    def to_string(self) -> str:
        match self.__tag:
            case lua_Type.NONE:
                return "none"
            case lua_Type.NIL:
                return "nil"
            case lua_Type.BOOLEAN:
                return "true" if self.__value else "false"
            case lua_Type.NUMBER:
                return str(self.__value)
            case lua_Type.STRING:
                return f"\"{self.__value}\""
            case _:
                # GDB already knows how to print addresses
                addr = self.__value.cast(VOID_POINTER)
                return f"{luaT_typenames[self.__tag]}: {str(addr)}"


class TStringPrinter:
    __data: str


    def __init__(self, val: gdb.Value):
        # Assume that pointers were already stripped, so we need to get `&ts`
        self.__data = getstr(val.address)


    def to_string(self):
        return self.__data


###=== }}} =====================================================================


###=== llex.h ============================================================== {{{


class TokenPrinter:
    """ NOTE(2025-04-20): Keep track of the field names in `llex.h`! """
    __type:    str
    __seminfo: str | float | None


    def __init__(self, token: gdb.Value):
        # In GDB, enums already have their proper names
        self.__type = str(token["type"])
        seminfo    = token["seminfo"]
        match self.__type:
            case "Token_Number":
                self.__seminfo = float(seminfo["r"])
            case "Token_String":
                # `seminfo.ts` is already `TString *` so no need to get address.
                self.__seminfo = getstr(seminfo["ts"])
            case "Token_Name":
                self.__seminfo = getstr(seminfo["ts"])
            case "Token_Eos":
                self.__seminfo = -1
            case _:
                self.__seminfo = None


    def to_string(self):
        if self.__seminfo:
            return f"{self.__type}: {self.__seminfo}"
        else:
            return self.__type


###=== }}} =====================================================================


###=== lparser.h =========================================================== {{{


class ExprPrinter:
    __kind:   gdb.Value
    __expr:   gdb.Value
    __info:   None | int | float
    __aux:    None | int
    __pretty: str


    def __set(self, *, info: str = None, aux: bool = False, nval: bool = False):
        args = [str(self.__kind).removeprefix("Expr_")]
        if info:
            self.__info = self.__expr["u"]["s"]["info"]
            args.append(f"{info} = {self.__info}")

        if aux:
            self.__aux  = self.__expr["u"]["s"]["aux"]
            actual: int = self.__aux
            if ISK(actual):
                kind   = "constant-index"
                actual = INDEXK(actual)
            else:
                kind = "register"
            args.append(f", {kind} = {actual}")

        if nval:
            self.__info = self.__expr["u"]["nval"]
            args.append(str(self.__info))

        if len(args) > 1:
            args.insert(1, ": ")

        self.__pretty = "".join(args)


    def __init__(self, expr: gdb.Value):
        # The ExprKind enum is already accounted for within GDB
        self.__kind = expr["kind"]
        self.__expr = expr
        match str(self.__kind):
            case "Expr_Constant":
                self.__set(info = "constant-index")
            case "Expr_Number":
                self.__set(nval = True)
            case "Expr_Local":
                self.__set(info = "register")
            case "Expr_Upvalue":
                self.__set(info = "upvalue-index")
            case "Expr_Global":
                self.__set(info = "constant-index")
            case "Expr_Index":
                self.__set(info = "table-register", aux = True)
            case "Expr_Jump":
                self.__set(info = "pc")
            case "Expr_Relocable":
                self.__set(info = "pc")
            case "Expr_Nonrelocable":
                self.__set(info = "register")
            case "Expr_Call":
                self.__set(info = "pc")
            case "Expr_Vararg":
                self.__set(info = "pc")
            case _:
                self.__set()


    def to_string(self) -> str:
        return self.__pretty


###=== }}} =====================================================================


lua_pretty_printers = {
    "Instruction": InstructionPrinter,
    "TValue":      TValuePrinter,
    "TString":     TStringPrinter,
    "Token":       TokenPrinter,
    "StkId":       TValuePrinter,
    "Expr":        ExprPrinter,
}


