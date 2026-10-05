"""Differential checks for the standalone raw-PAC preparer core."""
import contextlib
import io
import json
import os
import struct
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "native"))
from test_prepare_assets import pac
from path_fixtures import junction
import prepare_assets as python_prep
from asset_format import Decoder

NATIVE = ROOT / "build/prepare/prepare-assets-test.exe"


class NativePreparationTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(NATIVE.is_file(), "build-prepare-assets-test.cmd first")
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "build/prepare")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / "game"
        self.game.mkdir()
        pac(self.game / "data/GAME00000.pac", [
            (b"same", b"hello", 0), (b"same", b"Z" * 31, 1),
            (b"bad/\x81\x5c.tid", b"opaque", 0)])
        pac(self.game / "DLC/A/A00000.pac", [(b"dlc", b"DLC", 0)])

    def native(self, action, output, expected=0, game_id=0):
        run = subprocess.run([str(NATIVE), action, str(self.game), str(output), str(game_id)],
                             text=True, capture_output=True, timeout=30)
        self.assertEqual(run.returncode, expected, run.stderr)
        return run

    def python(self, output):
        with contextlib.redirect_stdout(io.StringIO()):
            return python_prep.prepare(self.game, output, 0, 1, Decoder())

    def test_invalid_game_ids_reject_before_output_creation(self):
        output = self.root / 'invalid-output'
        for game_id in (4, 32, 0xffffffff):
            for action in ('prepare', 'verify'):
                with self.subTest(game_id=game_id, action=action):
                    run = self.native(action, output, expected=1, game_id=game_id)
                    self.assertIn('Unknown game identity', run.stderr)
                    self.assertFalse(output.exists())

    def test_native_matches_python_and_reuses_both_directions(self):
        py, native = self.root / "python", self.root / "native"
        self.python(py)
        self.native("prepare", native)
        for name in ("base.manifest", "dlc.manifest"):
            self.assertEqual((py / name).read_bytes(), (native / name).read_bytes())
        self.assertEqual(self.python(native)["reused"], 4)
        self.assertIn("reused=4", self.native("prepare", py).stdout)
        self.native("verify", py)
        self.native("verify", native)
        self.assertEqual(python_prep.verify(native, self.game, 0)["files"], 4)

    def test_corruption_unknown_output_and_source_change_refuse(self):
        output = self.root / "output"
        self.native("prepare", output)
        pac_file = next(output.rglob("pack00000.pac"))
        pac_file.write_bytes(b"corrupt")
        self.native("prepare", output, expected=1)
        self.native("verify", output, expected=1)
        unknown = self.root / "unknown"
        unknown.mkdir()
        (unknown / "user.txt").write_text("keep")
        self.native("prepare", unknown, expected=1)
        self.assertEqual((unknown / "user.txt").read_text(), "keep")
        (self.game / "data/GAME.cpk").write_bytes(b"changed")
        self.native("verify", output, expected=1)

    def test_owner_reader_shared_with_settings_accepts_equivalent_json(self):
        for suffix in ("", "-adv-cg"):
            output = self.root / ("formatted" + suffix)
            self.native("prepare" + suffix, output)
            owner = output / "owner.json"
            document = json.loads(owner.read_text())
            text = json.dumps(dict(reversed(list(document.items()))), indent=2)
            owner.write_text(text.replace("half", "ha\\u006cf"), encoding="utf-8")
            self.native("prepare" + suffix, output)
            self.native("verify" + suffix, output)

    def test_owner_reader_rejects_bad_metadata_before_writes(self):
        output = self.root / "bad-owner"
        self.native("prepare", output)
        owner = output / "owner.json"
        original = json.loads(owner.read_text())
        compact = json.dumps(original, separators=(",", ":"))
        invalid = ["{", compact + " trailing",
                   compact.replace('"format":1', '"format":01'),
                   json.dumps(dict(original, transform="unknown-recipe")),
                   json.dumps(dict(original, transform=None)),
                   json.dumps(dict(original, extra="adv-cg-half24-box-v1")),
                   json.dumps(dict(original, game=3))]
        for text in invalid:
            with self.subTest(text=text):
                owner.write_text(text, encoding="utf-8")
                before = {p.relative_to(output): p.read_bytes()
                          for p in output.rglob("*") if p.is_file()}
                for action in ("prepare", "verify"):
                    run = self.native(action, output, expected=1)
                    self.assertIn("ownership metadata", run.stderr)
                self.assertEqual(before, {p.relative_to(output): p.read_bytes()
                                         for p in output.rglob("*") if p.is_file()})

    def test_cancel_and_resume_preserves_completed_work(self):
        output = self.root / "partial"
        self.native("prepare-cancel", output, expected=1)
        self.assertFalse((output / "base.manifest").exists())
        self.assertFalse(list(output.rglob("*.tmp-*")))
        self.native("prepare", output)
        self.native("verify", output)

    def test_changed_sources_and_source_overlap_refuse(self):
        output = self.root / "prepared"
        self.native("prepare", output)
        pac(self.game / "data/OTHER00000.pac", [(b"new", b"new", 0)])
        self.native("verify", output, expected=1)
        self.native("prepare", self.game / "data/generated", expected=1)
        self.native("prepare", self.game, expected=1)

    def test_linked_library_parent_prepares_resumes_and_verifies(self):
        library = self.root / "steam-library"
        junction(library, self.root)
        self.game = library / "game"
        output = library / "prepared"
        self.native("prepare", output)
        self.assertIn("reused=4", self.native("prepare", output).stdout)
        self.native("verify", output)
        self.native("verify", str(output) + os.sep)

    def test_links_inside_source_and_output_are_refused(self):
        output = self.root / "prepared"
        junction(self.game / "data/linked", self.game / "DLC")
        self.assertIn("Reparse point refused", self.native("prepare", output, expected=1).stderr)
        (self.game / "data/linked").rmdir()
        self.native("prepare", output)
        base = output / "base"
        moved = self.root / "moved-base"
        base.rename(moved)
        junction(base, moved)
        self.assertIn("Reparse point refused", self.native("prepare", output, expected=1).stderr)
        self.native("verify", output, expected=1)

    def test_output_alias_into_source_is_refused(self):
        alias = self.root / "source-alias"
        junction(alias, self.game / "data")
        run = self.native("prepare", alias / "generated", expected=1)
        self.assertFalse((self.game / "data/generated").exists())
        self.assertIn("Output overlaps original source tree", run.stderr)

    def test_rebirth3_dlc_is_included_in_new_profile(self):
        pac(self.game / "DLC/NRB3DLC000000003/DL0100000.pac", [(b"dlc/marker", b"RB3", 0)])
        output = self.root / "rb3-cg"
        self.native("prepare-adv-cg", output, game_id=2)
        self.native("verify-adv-cg", output, game_id=2)
        self.assertTrue((output / "dlc.manifest").exists())
        self.assertIn('"transform":"adv-cg-half24-box-v1"',
                      (output / "owner.json").read_text())

    def test_unknown_tid_at_threshold_stays_original_and_is_reported(self):
        def tid(height):
            width = 3072
            pixels = width * height * 4
            header = bytearray(128)
            header[:4] = b"TID\x90"
            struct.pack_into("<I", header, 4, 128 + pixels)
            struct.pack_into("<III", header, 0x44, width, height, 32)
            struct.pack_into("<II", header, 0x58, pixels, 128)
            return bytes(header) + bytes(pixels)

        below, at = tid(2047), tid(2048)
        pac(self.game / "data/GAME00001.pac", [
            (b"event\\ma\\9998\\tex_01.tid", below, 0),
            (b"event\\ma\\9999\\tex_01.tid", at, 0)], part=1)
        output = self.root / "unknown-cg"
        result = self.native("prepare-adv-cg", output)
        self.assertEqual(result.stderr.count("Uncataloged large ADV CG"), 1)
        self.native("verify-adv-cg", output)


if __name__ == "__main__":
    unittest.main()
