"""Validate the built PE32 X3DAudio proxy."""
import struct
import sys
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit("usage: rebirths_proxy_build_test.py <X3DAudio1_7.dll>")
data = Path(sys.argv[1]).read_bytes()


def u16(offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(offset):
    return struct.unpack_from("<I", data, offset)[0]


assert data[:2] == b"MZ"
nt = u32(0x3C)
assert data[nt:nt + 4] == b"PE\0\0"
assert u16(nt + 4) == 0x14C, "proxy must be x86 PE32"
optional = nt + 24
assert u16(optional) == 0x10B, "proxy must be PE32"
sections = u16(nt + 6)
section_table = optional + u16(nt + 20)


def rva_offset(rva):
    for index in range(sections):
        offset = section_table + index * 40
        size, address, raw_size, raw = u32(offset + 8), u32(offset + 12), u32(offset + 16), u32(offset + 20)
        if address <= rva < address + max(size, raw_size):
            return raw + rva - address
    raise AssertionError(f"unmapped RVA {rva:08x}")


def c_string(offset):
    return data[offset:data.index(b"\0", offset)].decode("ascii")


export_rva = u32(optional + 96)
export = rva_offset(export_rva)
assert Path(sys.argv[1]).name.lower() == "x3daudio1_7.dll", "unexpected proxy filename"
expected = {"X3DAudioInitialize", "X3DAudioCalculate"}
assert u32(export + 20) == len(expected) and u32(export + 24) == len(expected), "unexpected export count"
names = u32(export + 32)
actual = {c_string(rva_offset(u32(rva_offset(names) + index * 4))) for index in range(len(expected))}
assert actual == expected, (actual, expected)
print(f"Native proxy artifact OK: {sys.argv[1]}")
