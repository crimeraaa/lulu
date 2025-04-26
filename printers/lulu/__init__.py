# This is just for type annotations; the `gdb` module only exists within GDB!
import gdb # type: ignore
from dataclasses import dataclass
from typing import Final, Optional, Generator, TypeAlias

from ..odin import parser

ENABLE_MSFT_WORKAROUNDS: Final = True
"""
The VSCode C/C++ extension, as well as Windows Subsystem for Linux (WSL),
seem to not properly respect the `map` display hint.
"""


def register_printers(objfile: gdb.Objfile):
    """
    Overview:
    -   For our purposes, an `objfile` is simply the executable we're debugging.
    -   If we're debugging `lulu/bin/lulu` then that's the objfile.
    -   Each objfile has its own pretty printers list associated with it.
    -   So instead of poking at the global `gdb.pretty_printers`, we can instead
        modify the pretty printers list on a per-objfile basis.

    Usage:
    -   Create `lulu/bin/lulu-gdb.py` right next to `lulu/bin/lulu`.
    -   When invoking `gdb lulu/bin/lulu`, it will load `lulu/bin/lulu-gdb.py`
        if auto loading of scripts was enabled.

    Notes:
    -   GDB Python does NOT include the relative current directory in `sys.path`.
    -   Thus, to import anyting from `printers`, you need to do one of:
        1.  Add the absolute path of `lulu/printers` to `sys.path` manually.
        2.  Use `importlib.util` to directly load `lulu/printers/lulu.py`.

    Sample:
    ```
    # lulu/bin/lulu-gdb.py
    import gdb
    import os
    import os

    # Get the full path that contains `lulu/printers/lulu.py`
    LULU_PATH = os.path.realpath(os.path.join(os.path.dirname(__file__), ".."))
    if LULU_PATH not in sys.path:
        sys.path.insert(0, LULU_PATH)

    from printers import lulu
    lulu.register_printers(gdb.current_objfile())
    ```
    """
    objfile.pretty_printers.append(lookup_printer)


__parser = parser.Parser()
__saved: dict[str, parser.Result] = {}


def lookup_printer(val: gdb.Value):
    try:
        utype = str(val.type.unqualified())
        if utype in __printers:
            return __printers[utype](val)

        demangled, tag = parser.demangle(__parser, utype, __saved)
        match demangled.mode:
            case "array":
                """
                Likely impossible; Odin arrays just 100% C declarations so our
                demangler cannot (and *will* not) handle these cases.
                -   `[256]byte` becomes `byte [256]`.
                -   `[2][3]f32` becomes `f32 [2][3]`.
                -   `[16]^byte` becomes `byte *[16]`.
                -   `[8]^string` becomes `struct string *[8]`.

                Pointers to any of the above get very ugly very fast due to how
                C's type declarations work. E.g:
                -   `^[256]byte` becomes `byte (*)[256]`
                -   `^[2][3]f32` becomes `f32 (*)[2][3]`
                -   `^[16]^byte` becomes `byte *(*)[16]`
                -   `^[8]^string` becomes `struct string *(*)[8]`

                Want even more pain? Try arrays-of-pointers-to-arrays!
                -   `[2]^[3]f32` becomes `byte (*[2])[3]`
                -   `^[2]^[3]f32` becomes `byte (*(*)[2])[3]`

                Fortunately for us, GDB already knows how to deal with fixed-size
                arrays (and pointers thereof) so we don't need to create our own
                special logic.
                """
                pass
            case "slice":   return odin_Slice(val, tag)
            case "dynamic": return odin_Slice(val, tag, has_cap=True)
            case "map":     return odin_Map(val, tag)
    except:
        pass
    return None


VOID_PTR: Final     = gdb.lookup_type("void").pointer()
UINTPTR:  Final     = gdb.lookup_type("uintptr")
NULL:     Final     = gdb.Value(0).cast(VOID_PTR)
Iterator: TypeAlias = Generator[tuple[str, gdb.Value], str, gdb.Value]

###=== ODIN DATA TYPES ===================================================== {{{

class odin_String:
    """
    struct string {
        u8 *data;
        int len;
    }
    """
    DECL: Final = "struct string"

    __data: Final[gdb.Value]
    __len:  Final[int]

    def __init__(self, val: gdb.Value):
        self.__data = val["data"]
        self.__len  = int(val["len"])

    def to_string(self) -> str:
        # `u8 *` can also be dereferenced properly as a string
        return self.__data.string(encoding = "utf-8", length = self.__len)

    def display_hint(self) -> str:
        return 'string'


class odin_Slice:
    """
    struct []$T {
        T *data;
        int len;
    }

    struct [dynamic]$T {
        T *data;
        int len;
        int cap;
        struct runtime::Allocator allocator;
    }
    """
    __tag:  Final[str]
    __data: Final[gdb.Value]
    __len:  Final[int]
    __cap:  Final[Optional[int]]

    def __init__(self, val: gdb.Value, tag: str, has_cap = False):
        self.__tag  = tag
        self.__data = val["data"] # NOTE(2025-04-25): Must be a pointer!
        self.__len  = int(val["len"])
        self.__cap  = int(val["cap"]) if has_cap else None

    def children(self) -> Iterator:
        return self.__iter__()

    def __iter__(self) -> Iterator:
        for i in range(self.__len):
            yield str(i), (self.__data + i).dereference()

    def to_string(self) -> str:
        """ Because of the `'array'` display hint, the actual data is printed
        by GDB using the `children()` method. """
        info = f"len={self.__len}"
        if self.__cap is not None:
            info += f", cap={self.__cap}"
        return f"{self.__tag}{{{info}}}"

    def display_hint(self) -> str:
        return 'array'


class odin_Map:
    """
    Links:
    -   https://pkg.odin-lang.org/base/runtime/#Raw_Map
    -   https://gist.github.com/flga/30184a5b47a8b8ed0201fa33dd01dfe6#file-odin-gdb-pretty-printers-py

    Odin: ```map[$K]$V```

    C:
    ```

    // `struct{...}` is the very compressed Odin-style struct definition used as
    // the struct tag.
    #define DATA_TYPE_TAG \
        struct{key:$K,value:$V,hash:uintptr,key_cell:$K,value_cell:$V}

    struct map[$K]$V {
        struct DATA_TYPE_TAG *data;
        int len;
        struct runtime::Allocator allocator;
    }

    struct DATA_TYPE_TAG {
        $K key;
        $V value;
        uintptr hash;
        $K key_cell;
        $V value_cell;
    }
    ```
    """


    @dataclass
    class Cell:
        type:      gdb.Type
        cell_type: gdb.Type
        base_addr: int

        def cell_addr(self, index: int) -> int:
            """
            Overview:
            -   See `base/runtime/dynamic_map_internal.odin:map_cell_index_static()`,
                specifically the "worst case" branch.
            -   From `base/runtime/dynamic_map_internal.odin:Map_Cell`:
            -   The usual array[index] indexing for []T backed by a []Map_Cell(T)
                becomes a bit more involved as there now may be internal padding.
                The indexing now becomes

                N :: len(Map_Cell(T){}.data)
                i := index / N
                j := index % N
                cell[i].data[j]

            Links:
            -   https://github.com/odin-lang/Odin/blob/master/base/runtime/dynamic_map_internal.odin#L90
            """
            if self.type.sizeof == 0:
                return self.base_addr

            # How many $T can fit in a cacheline?
            elems_per_cell = self.cell_type.sizeof // self.type.sizeof
            cell_index = index // elems_per_cell
            data_index = index % elems_per_cell
            return (self.base_addr
                    + (cell_index * self.cell_type.sizeof)
                    + (data_index * self.type.sizeof))

        def cell_value(self, index: int) -> gdb.Value:
            addr = self.cell_addr(index)
            return gdb.Value(addr).cast(self.type.pointer()).dereference()


    # The lower 6 bits of `__data` are used as log2 of the actual capacity.
    MASK_CAP:           Final = 0b0011_1111

    __tag:              Final[str]
    __len:              Final[int]
    __cap:              Final[int]
    __key:              Final[Cell]
    __value:            Final[Cell]
    __tombstone_mask:   Final[int]
    __hashes:           Final[gdb.Value]

    def __init__(self, value: gdb.Value, tag: str):
        """
        Assumptions:
        -   The lower 6 bits of all pointers for this platform are never used.
        -   Thus, the map's capacity is stored in these bits as a log2.
        -   This also makes the assumption that the map's capacity is always a
            power of 2.
        -   The actual pointer is simply the data pointer with the lower 6
            bits zeroed out.

        Notes:
        -   Odin makes many optimizations with the map layout.
        -   We cannot simply dereference the pointer as-is.

        Links:
        -   https://github.com/odin-lang/Odin/blob/master/base/runtime/dynamic_map_internal.odin#L192
        """
        data        = value["data"]
        base_addr   = int(data) & ~self.MASK_CAP
        cap_log2    = int(data) & self.MASK_CAP

        self.__tag  = tag
        self.__len  = int(value["len"])
        self.__cap  = 1 << cap_log2 if cap_log2 else 0

        # For the following lines, please refer to:
        #   `base/runtime/dynamic_map_internal.odin:map_kvh_data_static()`
        # https://github.com/odin-lang/Odin/blob/master/base/runtime/dynamic_map_internal.odin#L805C1-L805C21
        # ks: [^]Map_Cell($K) = auto_cast map_data(transmute(Raw_Map)m)
        self.__key = odin_Map.Cell(
            type=data["key"].type,
            cell_type=data["key_cell"].type,
            base_addr=base_addr
        )

        # vs: [^]Map_Cell($V) = auto_cast map_cell_index_static(ks, cap(m))
        self.__value = odin_Map.Cell(
            type=data["value"].type,
            cell_type=data["value_cell"].type,
            base_addr=self.__key.cell_addr(self.__cap)
        )

        # Map_Hash :: uintptr
        # TOMBSTONE_MASK :: 1<<(size_of(Map_Hash)*8 - 1)
        # hs: [^]Map_Hash = auto_cast map_cell_index_static(vs, cap(m))
        hash_type = data["hash"].type
        self.__tombstone_mask = 1 << (hash_type.sizeof*8 - 1)

        hash_addr = self.__value.cell_addr(self.__cap)
        self.__hashes = gdb.Value(hash_addr).cast(hash_type.pointer())

    def children(self) -> tuple[str, gdb.Value]:
        return self.__iter__()

    def __iter__(self) -> Iterator:
        for i in range(self.__cap):
            hash = (self.__hashes + i).dereference()
            # Skip empty/deleted entries
            if hash == 0 or (hash & self.__tombstone_mask) != 0:
                continue

            key = self.__key.cell_value(i)
            if self.__value.type.sizeof == 0:
                yield str(i), key

            value = self.__value.cell_value(i)
            yield str(key), value

    def to_string(self) -> str:
        return f"{self.__tag}{{len={self.__len}, cap={self.__cap}}}"

    def display_hint(self) -> str:
        if self.__value.type.sizeof == 0:
            return "array"
        return "map" if not ENABLE_MSFT_WORKAROUNDS else None


###=== }}} =====================================================================


class lulu_Value:
    """
    ```
    struct lulu::[value.odin]::Value {
        enum lulu::[value.odin]::Value_Type type;
        union lulu::[value.odin]::Value_Data data;
    }
    ```
    """
    DECL:   Final = "struct lulu::[value.odin]::Value"

    __tag:  Final[str]
    __data: Final[gdb.Value]

    def __init__(self, val: gdb.Value):
        # In GDB, enums are already pretty-printed to their names
        self.__tag  = str(val["type"])
        self.__data = val["data"]

    def to_string(self) -> str:
        match self.__tag:
            # Python's builtin types
            case "Nil":     return "nil"
            case "Boolean": return str(bool(self.__data["boolean"]))
            case "Number":  return str(float(self.__data["number"]))
            # will (eventually) delegate to `lulu_OString`
            case "String":  return str(self.__data["ostring"])

        # Assuming ALL data pointers have the same representation
        # Meaning `(void *)value.ostring == (void *)value.table` for the same
        # `lulu::Value` instance.
        pointer = self.__data["table"].cast(VOID_PTR)
        return f"{self.__tag.lower()}: {pointer}"


class lulu_Object_Header:
    """
    struct lulu::[object.odin]::Object_Header {
        enum lulu::[value.odin]::Value_Type type;
        struct lulu::[object.odin]::Object_Header *prev;
    }
    """
    ...


class lulu_OString:
    """
    struct lulu::[string.odin]::OString {
        struct lulu::[object.odin]::Object_Header base;
        u32 hash;
        int len;
        u8  data[0];
    }
    """
    DECL:     Final = 'struct lulu::[string.odin]::OString'
    DECL_PTR: Final = DECL + " *"

    __data:   Final[gdb.Value]
    __len:    Final[int]

    def __init__(self, val: gdb.Value):
        # Don't call `.address()`; that gives us `u8 (*)[0]`
        self.__data = val["data"]
        self.__len  = int(val["len"])

    def to_string(self) -> str:
        return self.__data.string(encoding = "utf-8", length = self.__len)

    def display_hint(self) -> str:
        return 'string'


# Maybe easier to just hardcode the mangled names...
__printers: Final = {
    odin_String.DECL:       odin_String,
    lulu_Value.DECL:        lulu_Value,
    lulu_OString.DECL:      lulu_OString,
    lulu_OString.DECL_PTR:  lulu_OString,
}

