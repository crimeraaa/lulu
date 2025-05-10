# This is just for type annotations; the `gdb` module only exists within GDB!
import gdb # type: ignore
from typing import Final
from printers import odin

class __PrettyPrinter(gdb.printing.PrettyPrinter):
    """
    Usage:
    -   Create `lulu/bin/lulu-gdb.py` right next to `lulu/bin/lulu`.
    -   When invoking `gdb lulu/bin/lulu`, it will load `lulu/bin/lulu-gdb.py`
        if auto loading of scripts was enabled.

    Notes:
    -   GDB Python does NOT include the relative current directory in `sys.path`.
    -   We handle this by manually adding the lulu repo's directory to `sys.path`
        within `.gdbinit`.

    Sample:
    ```
    # lulu/bin/lulu-gdb.py
    import gdb
    from printers import odin, lulu

    inferior = gdb.current_objfile()
    gdb.printing.register_pretty_printer(inferior, lulu.pretty_printer)
    gdb.printing.register_pretty_printer(inferior, odin.pretty_printer)
    # Make sure to clean up the global namespace!
    del inferior
    ```
    """
    __printers: Final

    def __init__(self, name: str):
        """
        NOTE(2025-04-26):
        -   I'd love to use `gdb.printing.RegexpCollectionPrettyPrinter()`.
        -   However, based on the `__call__` implementation (e.g. in
            `/usr/share/gdb/python/gdb/printing.py`) it decares
            `typename = gdb.types.get_basic_type(val.type).tag`.
        -   This is bad for us, because pointers *don't* have tags!
        -   If that was `None`, it tries `typename = val.type.name`.
        -   That also doesn't work, because again, pointers don't have
            such information on their own.
        -   So even if our regex allows for pointers, the parent class
            literally does not allow you to work with pointers.
        """
        super().__init__(name)

        # Assuming demangled but fully-qualified names
        self.__printers = {
            # Structs
            "lulu.Instruction":  InstructionPrinter,
            "lulu.Token":        TokenPrinter,
            "lulu.Expr":         ExprPrinter,
            "lulu.Value":        ValuePrinter,
            "lulu.OString":      odin.StringPrinter,
            
            # Pointers thereof
            "^lulu.Instruction": InstructionPrinter,
            "^lulu.Token":       TokenPrinter,
            "^lulu.Expr":        ExprPrinter,
            "^lulu.Value":       ValuePrinter,
            "^lulu.OString":     odin.StringPrinter,
        }

    def __call__(self, val: gdb.Value):
        type_name, ok = odin.pretty_printer.demangle(val)
        if ok and (type_name in self.__printers):
            return self.__printers[type_name](val)
        return None


VOID_PTR: Final = gdb.lookup_type("void").pointer()
UINTPTR:  Final = gdb.lookup_type("uintptr")

class InstructionPrinter:
    """
    ```
    struct lulu::[opcode.odin]::Instruction {
        u16 b : 9;
        u16 c : 9;
        u16 a : 8;
        enum lulu::[opcode.odin]::OpCode op : 6;
    }
    ```
    """
    SIZE_B:  Final = 9
    SIZE_C:  Final = 9
    SIZE_A:  Final = 8
    SIZE_OP: Final = 6
    SIZE_Bx: Final = SIZE_B + SIZE_C
    
    MAX_Bx:  Final = (1 << SIZE_Bx) - 1
    MAX_sBx: Final = MAX_Bx >> 1

    __b:  int
    __c:  int
    __a:  int
    __op: str

    def __init__(self, ip: gdb.Value):
        self.__b  = int(ip['b'])
        self.__c  = int(ip['c'])
        self.__a  = int(ip['a'])
        self.__op = str(ip["op"])
    
    def to_string(self) -> str:
        out: list[str] = [f"{self.__op}: A={self.__a}"]
        match self.__op:
            case "Constant": out.append(f", Bx={self.__bx}")
            case "Jump":     out.append(f", sBx={self.__sbx}")
            case _: out.append(f", B={self.__b}, C={self.__c}")

        return ''.join(out)
    
    @property
    def __bx(self) -> int:
        return (self.__b << self.SIZE_B) | self.__c

    @property
    def __sbx(self) -> int:
        return self.__bx - self.MAX_sBx


class TokenPrinter:
    """
    ```
    #define LITERAL_TAG union{f64,^OString}

    struct lulu::[lexer.odin]::Token {
        enum lulu::[lexer.odin]::Token_Type type;
        struct string lexeme;
        int line;
        struct LITERAL_TAG literal;
    }
    ```
    """
    __type:   Final[gdb.Value]
    __lexeme: Final[gdb.Value]

    def __init__(self, token: gdb.Value):
        self.__type   = token["type"]
        self.__lexeme = token["lexeme"]

    def to_string(self) -> str:
        ttype = str(self.__type)
        word  = str(self.__lexeme)
        # Keyword or operator?
        if ttype.lower() == word or not word.isalpha():
            return ttype
        return f"{ttype}: {word}"


class ExprPrinter:
    """
    ```
    struct lulu::[expr.odin]::Expr [
        enum lulu::[expr.odin]::Expr_Type type;
        struct lulu::[expr.odin]::Expr_Info info;
        int jump_if_true;
        int jump_if_false;
    ]
    ```
    """
    __type: Final[gdb.Value]
    __info: Final[gdb.Value]
    
    def __init__(self, val: gdb.Value):
        self.__type = val["type"]
        self.__info = val["info"]
    
    def to_string(self) -> str:
        tag        = str(self.__type)
        info_name  = ""
        info_value = None
        extra      = ""
        match tag:
            case "Discharged":
                info_name  = "register"
                info_value = self.__info["reg"]
            case "Need_Register":
                info_name = "pc"
                info_value = self.__info["pc"]
            case "Number":
                info_name = "number"
                info_value = self.__info["number"]
            case "Constant":
                info_name = "index"
                info_value = self.__info["index"]
            case "Global":
                info_name = "index"
                info_name = self.__info["index"]
            case "Local":
                info_name = "register"
                info_value = self.__info["reg"]
            case "Table_Index":
                info_name  = "table(reg)"
                info_value = self.__info["table"]["reg"]
                extra      = f", key(reg) = {self.__info['table']['index']}"
            case "Jump":
                info_name  = "pc"
                info_value = self.__info["pc"]
            case _:
                return tag

        return f"{tag}: {info_name} = {info_value}" + extra


class ValuePrinter:
    """
    ```
    struct lulu::[value.odin]::Value {
        enum lulu::[value.odin]::Value_Type type;
        union lulu::[value.odin]::Value_Data data;
    }
    ```
    """
    __type: Final[str]
    __data: Final[gdb.Value]

    def __init__(self, val: gdb.Value):
        # In GDB, enums are already pretty-printed to their names
        self.__type = str(val["type"])
        self.__data = val["data"]

    def to_string(self) -> str:
        match self.__type:
            # Python's builtin types
            case "Nil":     return "nil"
            case "Boolean": return str(bool(self.__data["boolean"])).lower()
            case "Number":  return str(float(self.__data["number"]))
            # will (eventually) delegate to `lulu_OString`
            case "String":  return str(self.__data["ostring"])

        # Assuming ALL data pointers have the same representation
        # Meaning `(void *)value.ostring == (void *)value.table` for the same
        # `lulu::Value` instance.
        pointer = self.__data["table"].cast(VOID_PTR)
        return f"{self.__type.lower()}: {pointer}"


pretty_printer = __PrettyPrinter("lulu")
