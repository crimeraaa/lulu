import gdb  # Need to configure .vscode/settings.json for this to show in PyLance!
from enum import Enum
from typing import Final, Union


def lua_pretty_printers_lookup(val: gdb.Value):
    try:
        # Strip away `const` and/or `volatile`
        utype = val.type.unqualified()

        # Strip away pointers until we hit the most pointed-to type
        # We may have a void pointer, in which case `val.dereference()` will throw.
        while utype.code == gdb.TYPE_CODE_PTR:
            val   = val.dereference()
            utype = val.type.unqualified()
        return lua_pretty_printers[utype.name](val)
    except Exception:
        return None


gdb.pretty_printers.append(lua_pretty_printers_lookup)

###=== TOKEN PRINTER ======================================================= {{{


class TokenPrinter:
    __type:    str
    __seminfo: str | float | None


    def __init__(self, token: gdb.Value):
        # enum already have their proper names
        self.__type = str(token["type"])
        seminfo     = token["seminfo"]
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

        # Enquote all strings
        if isinstance(self.__seminfo, str):
            self.__seminfo = f"\"{self.__seminfo}\""


    def to_string(self):
        if self.__seminfo:
            return f"{self.__type}: {self.__seminfo}"
        else:
            return self.__type


###=== }}} =====================================================================

###=== INSTRUCTION PRINTER ================================================= {{{


OpCode: Final = gdb.lookup_type("OpCode")


class InstructionPrinter:
    __val: int


    def __init__(self, val: gdb.Value):
        self.__val = int(val)


    def to_string(self) -> str:
        try:
            ip   = self.__val
            op   = str(gdb.Value(GET_OPCODE(ip)).cast(OpCode))
            a    = GETARG_A(ip)

            args = [f"{op}: A={a}, "]
            if op in luaP_opmodes:
                match luaP_opmodes[op]:
                    case OpMode.iABx:
                        bx = GETARG_Bx(ip)
                        args.append(f"Bx={bx}")

                    case OpMode.iAsBx:
                        sbx = GETARG_sBx(ip)
                        args.append(f"sBx={sbx}")
            else:
                b = GETARG_B(ip)
                c = GETARG_C(ip)
                args.append(f"B={b}, C={c}")

            return "".join(args)
        except (ValueError, TypeError, IndexError, KeyError) as err:
            return repr(err)


###=== lopcodes.h ========================================================== {{{


SIZE_B:     Final =  9
SIZE_C:     Final =  SIZE_B
SIZE_Bx:    Final =  SIZE_B + SIZE_C
SIZE_A:     Final =  8
SIZE_OP:    Final =  6


MASK_Bx:    Final =  0b00000000_00000011_11111111_11111111
MASK_B:     Final =  0b00000000_00000000_00000001_11111111
MASK_C:     Final =  MASK_B
MASK_A:     Final =  0b00000000_00000000_00000000_11111111
MASK_OP:    Final =  0b00000000_00000000_00000000_00111111


OFFSET_OP:  Final =  0
OFFSET_A:   Final =  OFFSET_OP + SIZE_OP
OFFSET_C:   Final =  OFFSET_A  + SIZE_A
OFFSET_B:   Final =  OFFSET_C  + SIZE_C
OFFSET_Bx:  Final =  OFFSET_C


MAXARG_Bx:  Final = (1 << SIZE_Bx) - 1
MAXARG_sBx: Final =  MAXARG_Bx >> 1


class OpMode(Enum):
    iABC  = 1
    iABx  = 2
    iAsBx = 3


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


###=== }}} =====================================================================

###=== }}} =====================================================================

###=== TVALUE PRINTER ====================================================== {{{

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
Value = Union[None|bool|int|str|gdb.Value]


def bvalue(tvalue: gdb.Value) -> bool:
    return tvalue["value"]["b"]


def nvalue(tvalue: gdb.Value) -> float:
    return tvalue["value"]["n"]


def pvalue(tvalue: gdb.Value) -> gdb.Value:
    return tvalue["value"]["p"]


def gcvalue(tvalue: gdb.Value) -> gdb.Value:
    return tvalue["value"]["gc"]


def svalue(tvalue: gdb.Value) -> str:
    # `ts` is a union with appropriate padding. `tsv` is the actual string header.
    # For pointer arithmetic it is safter to use `ts`.
    # But `value.gc.ts` is *not* a pointer hence we need to get the address.
    return getstr(gcvalue(tvalue)["ts"].address)


def getstr(ts: gdb.Value) -> str:
    # addition should be overloaded for pointer types already
    # `const char *data = (const char *)(ts + 1)`
    data = (ts + 1).cast(CONST_CHAR_POINTER)

    # Only `char *` and variants thereof can safely use the `.string()` method.
    return data.string()


###=== }}} =====================================================================


class TValuePrinter:
    __value: Value
    __tag:   lua_Type


    def __init__(self, value: gdb.Value):
        tag = lua_Type(int(value["tt"]))
        actual: Value
        try:
            match tag:
                case lua_Type.NIL:           actual = None
                case lua_Type.NONE:          actual = None
                case lua_Type.BOOLEAN:       actual = bvalue(value)
                case lua_Type.LIGHTUSERDATA: actual = pvalue(value)
                case lua_Type.NUMBER:        actual = nvalue(value)
                case lua_Type.STRING:        actual = svalue(value)
                case _:
                    actual = gcvalue(value).cast(VOID_POINTER)
        except Exception as err:
            print(repr(err))
            return
        # Can't print `actual` in case it's a `gdb.Value`; that will cause us
        # to recurse indefinitely!
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


###=== }}} =====================================================================


lua_pretty_printers: Final = {
    "Instruction": InstructionPrinter,
    "TValue":      TValuePrinter,
    "StkId":       TValuePrinter,
    "Token":       TokenPrinter,
}
