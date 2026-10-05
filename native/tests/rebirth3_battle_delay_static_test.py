"""Verify the Re;Birth3 battle delay and lifecycle callsites on the original EXE."""
import hashlib
import os
import struct
from pathlib import Path

game = Path(os.environ.get("REBIRTH3_GAME_EXE", r"binaries\rebirth3\NeptuniaReBirth3.exe"))
if not game.is_absolute():
    game = Path(__file__).resolve().parents[3] / game
data = game.read_bytes()
assert hashlib.sha256(data).hexdigest() == "455ab306a0e44b6e97af1ce2f7c6cf3b56c0352aaa1d62d24fffc2b11088751b"

def word(offset): return struct.unpack_from("<H", data, offset)[0]
def dword(offset): return struct.unpack_from("<I", data, offset)[0]

nt = dword(0x3c)
assert data[nt:nt + 4] == b"PE\0\0" and word(nt + 4) == 0x14c
optional = nt + 24
assert word(optional) == 0x10b and dword(optional + 28) == 0x400000
section_count = word(nt + 6)
sections = optional + word(nt + 20)

def mapped(rva, size):
    for i in range(section_count):
        section = sections + 40 * i
        virtual_size, address, raw_size, raw = (dword(section + n) for n in (8, 12, 16, 20))
        if address <= rva and rva + size <= address + min(virtual_size, raw_size):
            return data[raw + rva - address:raw + rva - address + size]
    raise AssertionError(f"unmapped RVA {rva:#x}")

assert mapped(0x1d5c5a, 5) == bytes.fromhex("83f8147f13")
for callsite, target, expected in (
    (0x20895d, 0x208870, "e80effffff"),
    (0x20896a, 0x208470, "e801fbffff"),
    (0x20897a, 0x208410, "e891faffff"),
):
    actual = mapped(callsite, 5)
    assert actual == bytes.fromhex(expected), hex(callsite)
    displacement = struct.unpack_from("<i", actual, 1)[0]
    assert callsite + 5 + displacement == target, hex(callsite)

print(f"Re;Birth3 battle delay baseline and lifecycle calls OK: {game}")
