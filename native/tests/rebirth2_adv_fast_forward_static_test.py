"""Read-only Re;Birth2 fast-forward identity, callsite, and config verifier."""
import hashlib
import os
import struct
from pathlib import Path

from shipped_config import check_feature_defaults, read_config

EXPECTED_SHA256 = "f0b0b9fe40a26a001d0ac87702d51844f19900cfabe3d38680b885cc607c2fb2"
GAME = Path(os.environ.get("REBIRTH2_GAME_EXE", ""))
if not GAME.is_file():
    raise SystemExit("Set REBIRTH2_GAME_EXE to the supported NeptuniaReBirth2.exe")
data = GAME.read_bytes()
assert hashlib.sha256(data).hexdigest() == EXPECTED_SHA256, "unsupported executable SHA-256"

def u16(at): return struct.unpack_from("<H", data, at)[0]
def u32(at): return struct.unpack_from("<I", data, at)[0]

assert data[:2] == b"MZ"
nt = u32(0x3C)
assert data[nt:nt + 4] == b"PE\0\0" and u16(nt + 24) == 0x10B
assert u32(nt + 24 + 28) == 0x00400000
sections = u16(nt + 6)
section_table = nt + 24 + u16(nt + 20)

def rva_offset(rva):
    for index in range(sections):
        at = section_table + index * 40
        virtual_size, virtual_address, raw_size, raw = u32(at + 8), u32(at + 12), u32(at + 16), u32(at + 20)
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            return raw + rva - virtual_address
    raise AssertionError(f"RVA {rva:08x} is not mapped")

def check_call(rva, target):
    expected = b"\xe8" + struct.pack("<i", target - rva - 5)
    assert data[rva_offset(rva):rva_offset(rva) + 5] == expected

for site, target in (
    (0x804b9, 0x8beb0),
    (0x291a32, 0x228800),
    (0x294ed9, 0x228800),
    (0x9b90e, 0x9bc60),
    (0x7b89f, 0x22aa70),
    (0x813ba, 0x812b0),
    (0x00076900, 0x000a47b0),
    (0x0008a48e, 0x0008a190),
    (0x0007b8d9, 0x000835d0),
    (0x0008a3ea, 0x000a47b0),
    (0x000a3ec1, 0x000a47b0),
    (0x000751b0, 0x0009f6e0),
    (0x0009d5fc, 0x0009d650),
    (0x0008926e, 0x0008b340),
    (0x0007d863, 0x0007bfd0),
    (0x0007b177, 0x0007acf0),
    (0x0007b196, 0x0007ad70),
    (0x0007e0fe, 0x0007de50),
    (0x0007d89b, 0x000880e0),
    (0x0008028f, 0x00099080),
    (0x0007f868, 0x00088260),
    (0x0007d849, 0x000883e0),
    (0x230983, 0x2327b0),
    (0x230b61, 0x2327b0),
    (0x23398d, 0x2327b0),
    (0x230f3d, 0x2327b0),

):
    check_call(site, target)

check_call(0x8ce0d, 0x8ccf0)
check_call(0x80873, 0x81920)
check_call(0x807ff, 0x24b3b0)
check_call(0x287f87, 0x24b3b0)
check_call(0x288037, 0x24b3b0)
assert data[rva_offset(0x2e82a2):rva_offset(0x2e82a2)+6] == bytes.fromhex('ff154cb07300')
# Native battle-presence predicate: scene global VA 0x0084f214, task +0x12a7c8.
# The render-only failsafe reads the same fields with an extra null-scene guard.
assert data[rva_offset(0x197850):rva_offset(0x197850)+18] == bytes.fromhex('8b0d14f2840033c03981c8a712000f95c0c3')

check_feature_defaults(read_config(), {
    "AdvFastForward": "0", "AdvAutoSkip": "0", "SkipChapterIntros": "0",
})
print(f"Re;Birth2 ADV fast-forward static baseline and config contract OK: {GAME}")
