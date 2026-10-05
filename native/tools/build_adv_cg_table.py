"""Regenerate the pinned ADV CG transform table from local research catalogs.

Reads original installed PACs only. Never writes decoded assets to the repository.
"""

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "native"))
from asset_format import Decoder, inspect_archive  # noqa: E402
from catalog_data import TARGET_CATALOG  # noqa: E402

LIMIT = 24 * 1024 * 1024


def half_tid(source, width, height):
    assert source[:4] == b"TID\x90"
    assert len(source) == struct.unpack_from("<I", source, 4)[0] == 128 + width * height * 4
    assert struct.unpack_from("<III", source, 0x44) == (width, height, 32)
    assert struct.unpack_from("<II", source, 0x58) == (width * height * 4, 128)
    ow, oh = (width + 1) // 2, (height + 1) // 2
    output = bytearray(128 + ow * oh * 4)
    output[:128] = source[:128]
    struct.pack_into("<I", output, 4, len(output))
    struct.pack_into("<II", output, 0x44, ow, oh)
    struct.pack_into("<I", output, 0x58, len(output) - 128)
    pixels = memoryview(source)[128:]
    for y in range(oh):
        sy = y * 2
        rows = 2 if sy + 1 < height else 1
        sums = np.zeros((ow, 4), dtype=np.uint16)
        for dy in range(rows):
            row = np.frombuffer(pixels, dtype=np.uint8, count=width * 4,
                                offset=(sy + dy) * width * 4).reshape(width, 4)
            sums += row[::2]
            sums[:width // 2] += row[1::2]
        counts = rows * np.full((ow, 1), 2, dtype=np.uint16)
        if width & 1:
            counts[-1] = rows
        averaged = ((sums + counts // 2) // counts).astype(np.uint8)
        output[128 + y * ow * 4:128 + (y + 1) * ow * 4] = averaged.tobytes()
    return output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--research-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "native/include/adv_cg_specs.hpp")
    args = parser.parse_args()
    rows = []
    for target in TARGET_CATALOG:
        game_id = target['id']
        game = target['name'] if game_id != 3 else 'sega-hard-girls'
        catalog = json.loads((args.research_root / "reports" / game /
                              "advstill-catalog/catalog.json").read_text(encoding="utf-8"))
        exe = target['executable']
        baseline = args.research_root / "binaries" / game / exe
        assert hashlib.sha256(baseline.read_bytes()).hexdigest() == catalog["baseline_sha256"]
        assert hashlib.sha256((Path(catalog["source_root"]) / exe).read_bytes()).hexdigest() == catalog["baseline_sha256"]
        groups = catalog.get("advstill_sets", catalog.get("advstills"))
        chosen = [tile for group in groups for tile in group["textures"]
                  if tile["pixel_bytes"] >= LIMIT]
        archives = {}
        for tile in chosen:
            archive = tile["archive"].replace("/", "\\")
            if archive not in archives:
                path = Path(catalog["source_root"]) / archive
                with path.open("rb") as stream:
                    archives[archive] = inspect_archive(stream, path.name)[2]
            entry = archives[archive][tile["pac_entry"]]
            assert entry.ordinal == tile["pac_entry"]
            assert entry.name.decode("ascii").lower() == tile["name"].lower()
            path = Path(catalog["source_root"]) / archive
            with path.open("rb") as stream:
                source = b"".join(Decoder().decode(stream, entry))
            source_hash = hashlib.sha256(source).hexdigest()
            assert source_hash == tile["decoded_sha256"]
            result = half_tid(source, tile["width"], tile["height"])
            part = int(re.search(r"(\d{5})\.pac$", archive, re.I).group(1))
            rows.append((game_id, archive.lower().replace("\\", "/"),
                         part << 16 | entry.ordinal, tile["name"].lower(),
                         len(source), tile["width"], tile["height"], len(result),
                         source_hash, hashlib.sha256(result).hexdigest()))
            print(game, tile["name"], source_hash[:12], rows[-1][-1][:12], flush=True)
    assert [sum(row[0] == game for row in rows) for game in range(4)] == [12, 5, 27, 0]
    lines = ["#pragma once", "#include <array>", "#include <cstdint>",
             "// Generated from local original-PAC catalogs by build_adv_cg_table.py.",
             "namespace rebirths::advcg {",
             "constexpr uint32_t kMinPixelBytes = 24u * 1024u * 1024u;",
             "struct Spec { uint32_t game, id, sourceSize, width, height, outputSize;",
             "  const char* archive; const char* name; const char* sourceHash; const char* outputHash; };",
             f"constexpr std::array<Spec,{len(rows)}> kSpecs{{{{"]
    for game, archive, ident, name, size, width, height, out_size, src_hash, out_hash in rows:
        lines.append(f"  {{{game},{ident},{size},{width},{height},{out_size},"
                     f"\"{archive}\",\"{name.replace(chr(92), chr(92)*2)}\","
                     f"\"{src_hash}\",\"{out_hash}\"}},")
    lines += ["}};", "} // namespace rebirths::advcg", ""]
    args.output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
