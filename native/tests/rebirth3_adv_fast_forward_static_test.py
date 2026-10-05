"""Read-only Re;Birth3 fast-forward identity, callsite, and config verifier."""
import hashlib
import os
import struct
from pathlib import Path

from shipped_config import check_feature_defaults, read_config

EXPECTED_SHA256 = "455ab306a0e44b6e97af1ce2f7c6cf3b56c0352aaa1d62d24fffc2b11088751b"
GAME = Path(os.environ.get("REBIRTH3_GAME_EXE", ""))
if not GAME.is_file():
    raise SystemExit("Set REBIRTH3_GAME_EXE to the supported NeptuniaReBirth3.exe")
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
    (0x00079D60, 0x000A7DB0),
    (0x0008D9DA, 0x0008D6C0),
    (0x0007EE19, 0x00086B00),
    (0x0008D91A, 0x000A7DB0),
    (0x000A74C1, 0x000A7DB0),
    (0x000785C0, 0x000A2CD0),
    (0x000A0DCF, 0x000936F0),
    (0x0008E7E1, 0x0008EB70),
    (0x0007E697, 0x0007E210),
    (0x0007E6B6, 0x0007E290),
    (0x000815FE, 0x00081350),
    (0x00080D9B, 0x0008B610),
    (0x0008378F, 0x0009C620),
    (0x00082D68, 0x0008B790),
    (0x00080D49, 0x0008B910),
    (0x000A0BEC, 0x000A0C40),
    (0x0008C79E, 0x0008E890),
    (0x00080D63, 0x0007F4D0),
    (0x0009480C, 0x00090FC0),
    (0x00091BFD, 0x000914F0),
    (0x0026E113, 0x0026FF60),
    (0x0026E2F1, 0x0026FF60),
    (0x0027113D, 0x0026FF60),
    (0x0026E6E5, 0x0026FF60),
):
    check_call(site, target)

check_feature_defaults(read_config(), {"AdvFastForward": "0", "AdvAutoSkip": "0"})
print(f"Re;Birth3 ADV fast-forward static baseline and config contract OK: {GAME}")
