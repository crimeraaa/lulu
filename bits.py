from ctypes import c_uint8 as u8, c_uint16 as u16, c_uint32 as u32
from ctypes import c_int8 as i8, c_int16 as i16, c_int32 as i32
from typing import Union

C_Int = Union[u8|u16|u32|i8|i16|i32]

def print_range(name: str, lower: C_Int, upper: C_Int):
    print(f"{name:3} := [{lower.value}..{upper.value}]")

MIN_U8 = u8(0)  # 0b00000000
MAX_U8 = u8(-1) # 0b11111111
MAX_I8 = i8(MAX_U8.value >> 1) # 0b01111111
MIN_I8 = i8(~MAX_I8.value) # 0b10000000

print_range("u8", MIN_U8, MAX_U8)
print_range("i8", MIN_I8, MAX_I8)

MIN_U16 = u16(0)  # 0b00000000_00000000
MAX_U16 = u16(-1) # 0b11111111_11111111
MAX_I16 = i16(MAX_U16.value >> 1) # 0b011111111_11111111
MIN_I16 = i16(~MAX_I16.value) # 0b10000000_00000000

print_range("u16", MIN_U16, MAX_U16)
print_range("i16", MIN_I16, MAX_I16)

MIN_U32 = u32(0)  # 0b00000000_00000000_00000000_00000000
MAX_U32 = u32(-1) # 0b11111111_11111111_11111111_11111111
MAX_I32 = i32(MAX_U32.value >> 1) # 0b01111111_11111111_11111111_11111111
MIN_I32 = i32(~MAX_I32.value)     # 0b10000000_00000000_00000000_00000000

print_range("u32", MIN_U32, MAX_U32)
print_range("i32", MIN_I32, MAX_I32)
