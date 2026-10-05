"""Read-only identity, callsite, and shipped-config verifier for ADV fast-forward.

This verifier never launches or modifies a game.  Set REBIRTH1_GAME_EXE to the
supported Re;Birth1 executable before invoking it.
"""
import hashlib
import os
import struct
from pathlib import Path

from shipped_config import check_feature_defaults, read_config


EXPECTED_SHA256 = "0a6fdae55681019c9612f2da5e59c7539a2def22f66546695b8e9978ca88ab71"
GAME = Path(os.environ.get("REBIRTH1_GAME_EXE", ""))

if not GAME.is_file():
    raise SystemExit("Set REBIRTH1_GAME_EXE to the supported NeptuniaReBirth1.exe")

data = GAME.read_bytes()
assert hashlib.sha256(data).hexdigest() == EXPECTED_SHA256, "unsupported executable SHA-256"


def u16(at):
    return struct.unpack_from("<H", data, at)[0]


def u32(at):
    return struct.unpack_from("<I", data, at)[0]


assert data[:2] == b"MZ"
nt = u32(0x3C)
assert data[nt:nt + 4] == b"PE\0\0"
assert u16(nt + 24) == 0x10B, "only the verified x86 baseline is supported"
assert u32(nt + 24 + 28) == 0x00400000, "unexpected image base"
sections = u16(nt + 6)
section_table = nt + 24 + u16(nt + 20)


def rva_offset(rva):
    for index in range(sections):
        at = section_table + index * 40
        virtual_size, virtual_address, raw_size, raw = (u32(at + 8), u32(at + 12),
                                                         u32(at + 16), u32(at + 20))
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            offset = raw + rva - virtual_address
            assert offset < len(data)
            return offset
    raise AssertionError(f"RVA {rva:08x} is not mapped")


def check_call(rva, target):
    actual = data[rva_offset(rva):rva_offset(rva) + 5]
    expected = b"\xe8" + struct.pack("<i", target - rva - 5)
    assert actual == expected, f"RVA {rva:08x}: {actual.hex()} != {expected.hex()}"


# The normal proxy owns these thirteen native fast-forward retargets plus one
# six-byte final-present redirect.  Each source byte is checked before writes.
# Each original CALL is checked byte-for-byte and resolved from its RVA.
SITES = (
    (0x00015A3A, 0x00015930),    # CG pan completion; original readiness predicate retained
    (0x0000FA47, 0x0000F5C0),    # delay (bit 4)
    (0x0001EB1E, 0x0001E800),    # background load (bit 8)
    (0x00011F43, 0x000106D0),    # character setup (bit 1024)
    (0x0001D8EE, 0x0001F9E0),    # AdvBin creation (bit 2048)
    (0x00010039, 0x00017C50),    # AdvSe aggregate poll (bit 4096)
    (0x0001EA7A, 0x00039860),    # background track (bit 128)
    (0x00028567, 0x00039900),    # frame track (bit 256)
    (0x00028573, 0x00039900),
    (0x0002857F, 0x00039900),
    (0x00038F61, 0x00039860),    # fade track (bit 512)
    (0x001E59AE, 0x001E76D0),    # manual-window skin, steady state
    (0x001E5E5D, 0x001E76D0),    # manual-window skin, open/close path
)
for site, target in SITES:
    check_call(site, target)

# AdvAutoSkip owns two successful event-request epoch boundaries and three
# story-input consumers. The wrappers call native RB1 state transitions; they
# do not emulate a keyboard or controller.
AUTO_SKIP_SITES = (
    (0x00008B29, 0x0000D180),
    (0x00008B9C, 0x0000D180),
    (0x0000A1CE, 0x00009860),
    (0x0000A206, 0x00009860),
    (0x0000A2D8, 0x00009860),
)
for site, target in AUTO_SKIP_SITES:
    check_call(site, target)

# `CALL DWORD PTR DS:[0x72f04c]` at the verified preferred image base.  The
# runtime transaction derives this IAT operand from loaded_base + 0x32f04c.
assert data[rva_offset(0x002970C8):rva_offset(0x002970C8) + 6] == bytes.fromhex("ff154cf07200")

check_feature_defaults(read_config(), {"AdvFastForward": "0", "AdvAutoSkip": "0"})

print(f"Re;Birth1 ADV fast-forward static baseline and config contract OK: {GAME}")
