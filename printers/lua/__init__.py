import gdb  # type: ignore # Need to configure .vscode/settings.json for this to show in PyLance!
from . import lopcodes, lobject, llex, lparser


def pretty_printers_lookup(val: gdb.Value):
    # Strip away `const` and/or `volatile`
    utype = str(val.type.unqualified())
    if utype in __pretty_printers:
        return __pretty_printers[utype](val)
    return None


__pretty_printers = {
    # Lua 5.1.5 structs
    "Instruction":   lopcodes.InstructionPrinter,
    "TValue":        lobject.TValuePrinter,
    "union TString": lobject.TStringPrinter,
    "TString":       lobject.TStringPrinter,
    "Node":          lobject.NodePrinter,
    "LocVar":        lobject.LocVarPrinter,
    "Token":         llex.TokenPrinter,
    "Expr":          lparser.ExprPrinter,

    # Pointers thereof that never function as arrays
    "Token *":         llex.TokenPrinter,
    "union TString *": lobject.TStringPrinter,
    "TString *":       lobject.TStringPrinter,
    "Expr *":          lparser.ExprPrinter,
}

