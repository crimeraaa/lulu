import gdb  # type: ignore # Need to configure .vscode/settings.json for this to show in PyLance!
from .lopcodes import InstructionPrinter
from .lobject import LocVarPrinter, TValuePrinter, TStringPrinter
from .llex import TokenPrinter
from .lparser import ExprPrinter


def pretty_printers_lookup(val: gdb.Value):
    # Strip away `const` and/or `volatile`
    utype = str(val.type.unqualified())
    if utype in __pretty_printers:
        return __pretty_printers[utype](val)
    return None


__pretty_printers = {
    # Lua 5.1.5 structs
    "Instruction":   InstructionPrinter,
    "TValue":        TValuePrinter,
    "TString":       TStringPrinter,
    "LocVar":        LocVarPrinter,
    "Token":         TokenPrinter,
    "Expr":          ExprPrinter,

    # Pointers thereof
    "Instruction *": InstructionPrinter,
    "Token *":       TokenPrinter,
    "Expr *":        ExprPrinter,
    "TString *":     TStringPrinter,
    "LocVar *":      LocVarPrinter,
}

