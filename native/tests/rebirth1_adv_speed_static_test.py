"""Read-only identity and callsite verifier for the experimental Re;Birth1 ADV visual-speed patch.

This is intentionally not an installer and never launches or modifies the game.
Set REBIRTH1_GAME_EXE to the normal supported executable before running it.
"""
import hashlib
import os
import struct
from pathlib import Path

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
optional_size = u16(nt + 20)
section_table = nt + 24 + optional_size


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


def bytes_at(rva, count):
    offset = rva_offset(rva)
    return data[offset:offset + count]


def check(rva, expected):
    actual = bytes_at(rva, len(expected))
    assert actual == expected, f"RVA {rva:08x}: {actual.hex()} != {expected.hex()}"


def check_call(rva, target):
    actual = bytes_at(rva, 5)
    assert actual[0] == 0xE8, f"RVA {rva:08x}: expected CALL, got {actual.hex()}"
    displacement = struct.unpack_from("<i", actual, 1)[0]
    resolved = rva + 5 + displacement
    assert resolved == target, f"RVA {rva:08x}: target {resolved:08x} != {target:08x}"


# AdvTalkMsgWnd state-25: two adjacent cdecl calls to generic interpolation.
# Their target is 0x00439900; the following bytes prove the shared cleanup.
check(0x0003623E, bytes.fromhex("8d 47 3c 50 e8 b9 36 00 00 8d 5f 4c 53 e8 b0 36 00 00 f3 0f 10 47 54 0f 57 c9 83 c4 08"))
check(0x00039900, bytes.fromhex("55 8b ec 56 8b 75 08 0f 57 c0 0f 2e 46 08 9f f6 c4 44 7b"))

# The generic helper's terminal stores are required for a wrapper to reproduce
# its terminal state: current=target, velocity=0, final-copy=target.
check(0x0003997A, bytes.fromhex("8b 46 04 89 06 c7 46 08 00 00 00 00 89 46 0c"))

# Optional face-window transition candidate. These are separate cdecl calls to
# the same interpolator and share an ADD ESP,8 cleanup; they are deliberately
# not bundled with the message-window toggle by the verifier.
check(0x0003533E, bytes.fromhex("e8 bd 45 00 00"))
check(0x0003534A, bytes.fromhex("e8 b1 45 00 00 0f be 87 4c 02 00 00 83 c4 08"))

# Independent AdvEffect/AdvSsa visual tracks use the same one-argument helper.
for callsite in (0x00026C5C, 0x00026C68, 0x00026C74, 0x00026C80,
                 0x00026C8C, 0x00026C98, 0x00026CA4, 0x00026CB0,
                 0x000313AF):
    check_call(callsite, 0x00039900)
check(0x00026CB5, bytes.fromhex("f3 0f 10 0e 83 c4 20"))

# Nested Cw/effect-resource presentation record. This stays separate from the
# outer Effect group because the same helper is used by the advCw lifecycle.
check(0x000280A0, bytes.fromhex("8d be a8 01 00 00 57 e8"))
check_call(0x000280A7, 0x00039900)
check(0x000280AC, bytes.fromhex("56 e8 8e 02 00 00"))

# AdvBgWork's scaled local record. Adjacent custom fastcall tracks are not
# eligible for this generic terminalizer and are deliberately excluded.
check(0x0001EA79, bytes.fromhex("56 e8"))
check_call(0x0001EA7A, 0x00039860)

# AdvFrameWork owns these three presentation records in its update callback.
for callsite in (0x00028567, 0x00028573, 0x0002857F):
    check_call(callsite, 0x00039900)

# AdvFade is a separate two-argument, multiplier-aware helper call.
check_call(0x00038F61, 0x00039860)
check(0x00038F66, bytes.fromhex("83 c4 08 33 c0"))

# uiEventViewerFade has its own local record and lifecycle; it is deliberately
# separate from AdvFade rather than changing the generic interpolator.
check(0x000D919A, bytes.fromhex("8d 47 50 50 e8"))
check_call(0x000D919E, 0x00039900)
check(0x000D91A3, bytes.fromhex("83 c4 04"))

# Candidate choice-list snap is not installed by the proxy. Preserve the exact
# eight-byte instruction so any later opt-in implementation cannot silently
# target a different global blend consumer.
check(0x0003C0D1, bytes.fromhex("f3 0f 59 0d 4c 05 73 00"))

print(f"Re;Birth1 ADV visual-speed static baseline OK: {GAME}")
