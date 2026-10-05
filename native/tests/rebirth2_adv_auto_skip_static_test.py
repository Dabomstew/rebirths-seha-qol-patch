"""Read-only Re;Birth2 auto-skip baseline and callsite verifier."""
import hashlib
import os
import struct
from pathlib import Path


GAME = Path(os.environ.get("REBIRTH2_GAME_EXE", r"binaries\rebirth2\NeptuniaReBirth2.exe"))
if not GAME.is_absolute():
    GAME = (Path(__file__).resolve().parents[3] / GAME).resolve()
data = GAME.read_bytes()
assert hashlib.sha256(data).hexdigest() == "f0b0b9fe40a26a001d0ac87702d51844f19900cfabe3d38680b885cc607c2fb2"


def u16(offset): return struct.unpack_from("<H", data, offset)[0]
def u32(offset): return struct.unpack_from("<I", data, offset)[0]


nt = u32(0x3C)
assert data[nt:nt + 4] == b"PE\0\0" and u16(nt + 4) == 0x14C
optional = nt + 24
assert u16(optional) == 0x10B and u32(optional + 28) == 0x400000
sections = u16(nt + 6); section_table = optional + u16(nt + 20)


def mapped(rva, size):
    for index in range(sections):
        section = section_table + index * 40
        virtual_size, address, raw_size, raw = (u32(section + 8), u32(section + 12), u32(section + 16), u32(section + 20))
        if address <= rva and rva + size <= address + min(virtual_size, raw_size):
            return data[raw + rva - address:raw + rva - address + size]
    raise AssertionError(f"unmapped RVA {rva:#x}")


sites = {475865: b'\xe8\x12F\x00\x00', 475980: b'\xe8\x9fE\x00\x00', 481614: b'\xe8\x8d\xf6\xff\xff', 481674: b'\xe8Q\xf6\xff\xff', 481864: b'\xe8\x93\xf5\xff\xff', 507772: b'\xe8\x0f\x84\xff\xff'}
sites[0x7f69c] = b'\xe8' + struct.pack('<i', 0x74390 - 0x7f69c - 5)
for rva, expected in sites.items():
    assert mapped(rva,len(expected)) == expected, hex(rva)
print("RB2 seven auto-skip CALLs and baseline verified")

# Independent features must not own the same instruction bytes.
import re
ff = (Path(__file__).parents[1] / 'src/rebirth2_adv_fast_forward.cpp').read_text()
ff_sites = {int(v,16) for v in re.findall(r'\{(0x[0-9a-f]+),\s*\{0xe8',ff)}
assert ff_sites and not set(sites).intersection(ff_sites)
# Full EAX request-success producer and original input's explicit true/false.
assert mapped(0x78961, 9) == bytes.fromhex('33c03947145f0f95c0')
assert mapped(0x750a1, 5) == bytes.fromhex('b801000000')
assert mapped(0x750de, 2) == bytes.fromhex('33c0')
