"""Read-only Re;Birth3 auto-skip baseline and callsite verifier."""
import hashlib
import os
import struct
from pathlib import Path


GAME = Path(os.environ.get("REBIRTH3_GAME_EXE", r"binaries\rebirth3\NeptuniaReBirth3.exe"))
if not GAME.is_absolute():
    GAME = (Path(__file__).resolve().parents[3] / GAME).resolve()
data = GAME.read_bytes()
assert hashlib.sha256(data).hexdigest() == "455ab306a0e44b6e97af1ce2f7c6cf3b56c0352aaa1d62d24fffc2b11088751b"


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


sites = {
    0x775E9: bytes.fromhex("e872470000"),
    0x7765C: bytes.fromhex("e8ff460000"),
    0x78D8E: bytes.fromhex("e84df6ffff"),
    0x78DC6: bytes.fromhex("e815f6ffff"),
    0x78E98: bytes.fromhex("e843f5ffff"),
    0x78934: bytes.fromhex("e8870d0000"),
    0x82B9C: bytes.fromhex("e8ff4affff"),
    0x15C36F: bytes.fromhex("e8acf7ffff"),
    0x15BB36: bytes.fromhex("e8a5e31000"),
    0x15C2C8: bytes.fromhex("e8f3d3f1ff"),
    0x15C44E: bytes.fromhex("e83df6ffff"),
    0x15C364: bytes.fromhex("e807f9ffff"),
    0x15C459: bytes.fromhex("e812f8ffff"),
    0x15BC8A: bytes.fromhex("e851e21000"),
    0x15B8D6: bytes.fromhex("e815220000"),
}
for rva, expected in sites.items():
    assert mapped(rva, len(expected)) == expected, hex(rva)

assert mapped(0x77990, 16) == bytes.fromhex("558bec568b750856e803fdffff83c404")
assert mapped(0x7BD60, 16) == bytes.fromhex("558bec83ec44a1e07e890033c58945fc")
assert mapped(0x7CFD0, 16) == bytes.fromhex("558bec807d0c00750433c05dc3578b7d")
assert mapped(0x18BD90, 16) == bytes.fromhex("558bec8b450885c0750432c05dc350e8")
assert mapped(0x18BD70, 16) == bytes.fromhex("558bec8b450885c0740950e8901d0e00")
print(f"Re;Birth3 ADV auto-skip baseline and eleven AutoSkip and four NepstationSkip callsites OK: {GAME}")
