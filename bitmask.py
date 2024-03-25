#!/usr/bin/python3

import ctypes
import sys

BITFILL  = 1 # Bit places to be filled out with 1's, from right to left.
POSITION = 0 # Adjust for a position offset.

TYPENAME = "Instruction"
BITMASK  = ctypes.c_uint32(-1).value

FILLSTR  = f"(~({TYPENAME})0)" # (~(Instruction)0)
MASKSTR  = f"({FILLSTR} << N)" # ({FILL} << {N})
FLIPSTR  = f"(~{MASKSTR})"     # (~{MASK})
SHIFTSTR = f"({FLIPSTR} << pos)"
EQUATION = f"MASK1(N, pos) => {SHIFTSTR}"


def submiddle(src: str, mid: str, sub: str):
    """ Hard to explain, so let's just use an example.
    
    Given:
    - `src="((~(Byte)0) << N)"`
    - `mid="(~(Byte)0)"`
    - `sub="FILL"`

    Goal:
    - Substitute the entire occurence of `mid` in `src` with `sub`.

    1. Split `src` with `mid`, giving us a `list[str]` of `["(", " << N"]`.
    2. Concatenate the first element, `"FILL"`, and the second element.
    3. Return the result `"(FILL << N)"`.
    """
    splits = str.split(src, mid)
    return str.join("", [splits[0], sub, splits[1]])


SUBMASK = submiddle(MASKSTR, FILLSTR, "FILL")
SUBFLIP = submiddle(FLIPSTR, MASKSTR, "MASK")
SUBSHIFT = submiddle(SHIFTSTR, FLIPSTR, "FLIP")


def new_uint(value: int):
    return value & BITMASK


def param(name: str, value: int, isbinary = True):
    show = bin(value) if isbinary else str(value)
    print(f"\t{name:12} := {show}")


def onebits_fill():
    fill = new_uint(-1)
    print(f"FILL => {FILLSTR} => {bin(fill)}")
    print()
    return fill

    
def fill_to_mask(fill: int, N: int):
    mask = new_uint(fill << N)
    print(f"MASK => {SUBMASK}")
    param("N", N, False)
    param("FILL", fill)
    param("FILL << N", mask)
    print()
    return mask


def mask_to_flip(mask: int):
    flip = new_uint(~mask)
    print(f"FLIP => {SUBFLIP}")
    param("MASK", mask)
    param("~MASK", flip)
    print()
    return flip


def flip_to_shift(flip: int, pos: int):
    shift = new_uint(flip << pos)
    print(f"SHIFT => {SUBSHIFT}")
    param("FLIP", flip)
    param("POS", pos)
    param("FLIP << pos", shift)
    print()
    return shift


def mask1(N: int, pos: int):
    fill = onebits_fill()
    mask = fill_to_mask(fill, N)
    flip = mask_to_flip(mask)
    return flip_to_shift(flip, pos)


def mask2(N: int, pos: int):
    return new_uint(~flip_to_shift(N, pos))


def main(argc: int, argv: list[str]):
    if argc == 1:
        N   = BITFILL
        pos = POSITION
    elif argc == 3:
        N = int(argv[1])
        pos = int(argv[2])
    else:
        print("Expected 0 or 2 non-script arguments exactly.")
        sys.exit(2)

    print(f"N   := {N}")
    print(f"pos := {pos}")
    print()
    print(EQUATION)
    print(f"\tFILL  := {FILLSTR}")
    print(f"\tMASK  := {SUBMASK}")
    print(f"\tFLIP  := {SUBFLIP}")
    print(f"\tSHIFT := {SUBSHIFT}")
    print()
    shift = mask1(N, pos)
    
    print(f"MASK1(N, pos) => {bin(shift)}")
    return 0


if __name__=="__main__":
    main(len(sys.argv), sys.argv)
