"""Structural checks for pinned, per-game ADV CG transformation identities."""

import re
from pathlib import Path
import unittest


HEADER = Path(__file__).resolve().parents[1] / "include/adv_cg_specs.hpp"
ROW = re.compile(r'^  \{(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),"([^"]+)","([^"]+)","([a-f0-9]{64})","([a-f0-9]{64})"\},$', re.M)


class AdvCgTableTests(unittest.TestCase):
    def test_threshold_identity_and_pilot_compatibility(self):
        rows = ROW.findall(HEADER.read_text(encoding="utf-8"))
        self.assertEqual(len(rows), 44)
        counts = [sum(int(row[0]) == game for row in rows) for game in range(4)]
        self.assertEqual(counts, [12, 5, 27, 0])
        keys = set()
        for game, ident, size, width, height, output, archive, name, source_hash, output_hash in rows:
            game, ident, size, width, height, output = map(int, (game, ident, size, width, height, output))
            self.assertGreaterEqual(width * height * 4, 24 * 1024 * 1024)
            self.assertEqual(size, 128 + width * height * 4)
            self.assertEqual(output, 128 + ((width + 1) // 2) * ((height + 1) // 2) * 4)
            self.assertTrue(archive.startswith(("data/", "dlc/")))
            self.assertTrue(name.startswith("event\\\\ma\\\\"))
            self.assertTrue(name.endswith(".tid"))
            key = (game, archive, ident)
            self.assertNotIn(key, keys)
            keys.add(key)
        pilot = next(row for row in rows if row[0] == "2" and row[1] == str((1 << 16) | 264))
        self.assertEqual(pilot[-1], "9317f1b273cb874aca6958b29e8269f434a843356a6d07db3728f0a1edccaeb0")


if __name__ == "__main__":
    unittest.main()
