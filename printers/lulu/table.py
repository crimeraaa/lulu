import gdb # type: ignore

from .. import base

class Entry_Printer:
    key: gdb.Value
    value: gdb.Value

    def __init__(self, v: gdb.Value):
        self.key   = v["key"]
        self.value = v["value"]

    # def children(self) -> base.Iterator:
    #     yield str(self.key), self.value

    def to_string(self) -> str:
        return f"[{self.key}] = {self.value}"

    def display_hint(self) -> base.Optional[str]:
        return base.display_hint("map")

