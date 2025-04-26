import gdb # type: ignore
from typing import Final, Generator, TypeAlias
from dataclasses import dataclass
from . import parser

Iterator: TypeAlias = Generator[tuple[str, gdb.Value], str, gdb.Value]

ENABLE_MSFT_WORKAROUNDS: Final = True
"""
The VSCode C/C++ extension, as well as Windows Subsystem for Linux (WSL),
seem to not properly respect the `map` display hint.
"""


class __PrettyPrinter(gdb.printing.PrettyPrinter):
    __parser:   parser.Parser
    __saved:    dict[str, str]

    def __init__(self, name: str):
        super().__init__(name)
        self.__parser = parser.Parser()
        self.__saved  = {}

    def __call__(self, val: gdb.Value):
        tag = self.demangle(val)
        if tag == "string":
            return StringPrinter(val)
        elif tag.startswith("[]"):
            return SlicePrinter(val, tag)
        elif tag.startswith("[dynamic]"):
            return DynamicPrinter(val, tag)
        elif tag.startswith("map"):
            return MapPrinter(val, tag)
        return None

    def demangle(self, val: gdb.Value) -> str:
        return parser.demangle(self.__parser, str(val.type), self.__saved)


class StringPrinter:
    """
    struct string {
        u8 *data;
        int len;
    }
    """
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


class SlicePrinter:
    """
    struct []$T {
        T *data;
        int len;
    }
    """
    __tag:  Final[str]
    __data: Final[gdb.Value]
    __len:  Final[int]

    def __init__(self, val: gdb.Value, tag: str):
        self.__tag  = tag
        self.__data = val["data"] # NOTE(2025-04-25): Must be a pointer!
        self.__len  = int(val["len"])

    def children(self) -> Iterator:
        return self.__iter__()

    def __iter__(self) -> Iterator:
        for i in range(self.__len):
            yield str(i), (self.__data + i).dereference()

    def to_string(self) -> str:
        """ Because of the `'array'` display hint, the actual data is printed
        by GDB using the `children()` method. """
        return f"{self.__tag}{{len={self.__len}}}"

    def display_hint(self) -> str:
        return 'array'


class DynamicPrinter:
    """
    struct [dynamic]$T {
        T *data;
        int len;
        int cap;
        runtime::Allocator allocator;
    }
    """
    __tag:  Final[str]
    __data: Final[gdb.Value]
    __len:  Final[int]
    __cap:  Final[int]

    def __init__(self, val: gdb.Value, tag: str):
        self.__tag  = tag
        self.__data = val["data"]
        self.__len  = int(val["len"])
        self.__cap  = int(val["cap"])

    def children(self) -> Iterator:
        return self.__iter__()

    def __iter__(self) -> Iterator:
        for i in range(self.__len):
            yield str(i), (self.__data + i).dereference()

    def to_string(self) -> str:
        """ Because of the `'array'` display hint, the actual data is printed
        by GDB using the `children()` method. """
        return f"{self.__tag}{{len={self.__len}, cap={self.__cap}}}"

    def display_hint(self) -> str:
        return 'array'


@dataclass
class MapCell:
    elem_type:      gdb.Type
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
        if self.elem_type.sizeof == 0:
            return self.base_addr

        # How many $T can fit in a cacheline?
        elems_per_cell = self.cell_type.sizeof // self.elem_type.sizeof
        cell_index = index // elems_per_cell
        data_index = index % elems_per_cell
        return (self.base_addr
                + (cell_index * self.cell_type.sizeof)
                + (data_index * self.elem_type.sizeof))

    def cell_value(self, index: int) -> gdb.Value:
        addr = self.cell_addr(index)
        return gdb.Value(addr).cast(self.elem_type.pointer()).dereference()


class MapPrinter:
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


    # The lower 6 bits of `__data` are used as log2 of the actual capacity.
    MASK_CAP:           Final = 0b0011_1111

    __tag:              Final[str]
    __len:              Final[int]
    __cap:              Final[int]
    __key:              Final[MapCell]
    __value:            Final[MapCell]
    __tombstone_mask:   Final[int]
    __hashes:           Final[gdb.Value]

    def __init__(self, val: gdb.Value, tag: str):
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
        data        = val["data"]
        base_addr   = int(data) & ~self.MASK_CAP
        cap_log2    = int(data) & self.MASK_CAP

        self.__tag  = tag
        self.__len  = int(val["len"])
        self.__cap  = 1 << cap_log2 if cap_log2 else 0

        # For the following lines, please refer to:
        #   `base/runtime/dynamic_map_internal.odin:map_kvh_data_static()`
        # https://github.com/odin-lang/Odin/blob/master/base/runtime/dynamic_map_internal.odin#L805C1-L805C21
        # ks: [^]Map_Cell($K) = auto_cast map_data(transmute(Raw_Map)m)
        self.__key = MapCell(
            elem_type=data["key"].type,
            cell_type=data["key_cell"].type,
            base_addr=base_addr
        )

        # vs: [^]Map_Cell($V) = auto_cast map_cell_index_static(ks, cap(m))
        self.__value = MapCell(
            elem_type=data["value"].type,
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

    def children(self) -> Iterator:
        return self.__iter__()

    def __iter__(self) -> Iterator:
        for i in range(self.__cap):
            hash = (self.__hashes + i).dereference()
            # Skip empty/deleted entries
            if hash == 0 or (hash & self.__tombstone_mask) != 0:
                continue

            key = self.__key.cell_value(i)
            if self.__value.elem_type.sizeof == 0:
                yield str(i), key

            value = self.__value.cell_value(i)
            yield str(key), value

    def to_string(self) -> str:
        return f"{self.__tag}{{len={self.__len}, cap={self.__cap}}}"

    def display_hint(self) -> str:
        if self.__value.elem_type.sizeof == 0:
            return "array"
        return "map" if not ENABLE_MSFT_WORKAROUNDS else None


pretty_printer = __PrettyPrinter("odin_pretty_printer")
