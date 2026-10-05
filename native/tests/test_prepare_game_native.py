"""Isolated preparer installer tests; game EXEs are copied, never modified in place."""
import hashlib
import configparser
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from test_prepare_assets import pac
from path_fixtures import junction

ROOT = Path(__file__).resolve().parents[2]
PARENT = ROOT.parent
APP = ROOT / "build/prepare/prepare-game-test.exe"
BASELINES = [
    ("rebirth1", "Neptunia Rebirth1", "NeptuniaReBirth1.exe", "0a6fdae55681019c9612f2da5e59c7539a2def22f66546695b8e9978ca88ab71"),
    ("rebirth2", "Neptunia Rebirth2", "NeptuniaReBirth2.exe", "f0b0b9fe40a26a001d0ac87702d51844f19900cfabe3d38680b885cc607c2fb2"),
    ("rebirth3", "Hyperdimension Neptunia Re;Birth3", "NeptuniaReBirth3.exe", "455ab306a0e44b6e97af1ce2f7c6cf3b56c0352aaa1d62d24fffc2b11088751b"),
    ("sega-hard-girls", "Superdimension Neptune VS Sega Hard Girls", "Neptune VSSega Hard Girls.exe", "af9550b0b5ab90810e31274066f13b3e1b224ef5960951a44d2acd53903523e5"),
]
NTCORE = [
    "02169eeb7219128cd238e93c58530577a83569d6376d030bf84acfdff2ff8941",
    "8abd1b2272d198626853800f3b50cfaa580d2f797ec2b48dbef745b6c9950937",
    "b46751763f470a0d57eeaf590cb95d85118ebda6e0e86765a93a1e2280d79b01",
    "d450aa5f92f6025634cbb81edbb59ad3713b07a2c920a3b123be584d47edc9c9",
]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class PreparerFixture(unittest.TestCase):
    def setUp(self):
        self.assertTrue(APP.is_file(), "build-prepare-game-test.cmd first")
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "build/prepare")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fixture(self, index):
        game_id, folder, exe, expected = BASELINES[index]
        source = Path(os.environ["PATCH_BASELINE_DIR"]) / game_id / exe
        self.assertTrue(source.is_file(), source)
        self.assertEqual(digest(source), expected)
        game = self.root / folder
        game.mkdir()
        shutil.copy2(source, game / exe)
        pac(game / "data/GAME00000.pac", [(b"sample", b"data", 0)])
        return game, game / exe

    def run_app(self, action, game, *args, expected=0, env=None):
        run = subprocess.run([str(APP), action, str(game), *map(str, args)],
                             text=True, capture_output=True, env=env, timeout=30)
        self.assertEqual(run.returncode, expected, run.stdout + run.stderr)
        return run

    def test_all_feature_defaults_values_and_upgrade_policy(self):
        # Independent policy oracle; do not import generated feature metadata.
        defaults = dict(UncompressedAssets=0, FastTextureConversion=1, AdvFastForward=0,
                        AdvAutoSkip=0, NepstationSkip=0, SkipTutorials=1, BattleLoadDelaySkip=0,
                        SkipChapterIntros=0, DungeonMovementFix=1)
        restricted = {'NepstationSkip': 2, 'BattleLoadDelaySkip': 2, 'SkipChapterIntros': 1, 'DungeonMovementFix': 3}
        for index in range(4):
            with self.subTest(game=index):
                game, _ = self.fixture(index)
                proxy_dir = game / 'Birth3' if index == 2 else game
                proxy_dir.mkdir(exist_ok=True)
                config = proxy_dir / 'rebirths-patches.ini'
                def read_settings():
                    text = self.run_app('feature-settings', game).stdout
                    return {k: int(v) for k, v in (line.split('=') for line in text.splitlines())}
                fresh = dict(defaults, UncompressedAssets=1)
                self.assertEqual(read_settings(), fresh)
                config.write_text('[Patches]\n', encoding='utf-8')
                self.assertEqual(read_settings(), defaults)
                for value in (0, 1):
                    text = '[Patches]\n' + ''.join(f'{key}={value}\n' for key in defaults)
                    # Preserve-only and unsupported keys must survive writes, even when
                    # managed controls request the opposite value.
                    text += f'FastFaceTextureCreation={value}\n[CustomSection]\nCustom=keep\n'
                    config.write_text(text, encoding='utf-8')
                    self.assertEqual(read_settings(), {k: value for k in defaults})
                    requested = 1 - value
                    action = 'prepare-features-on' if requested else 'prepare-features-off'
                    self.run_app(action, game)
                    parsed = configparser.ConfigParser(interpolation=None)
                    parsed.optionxform = str
                    parsed.read(config, encoding='utf-8')
                    for key in defaults:
                        supported = key not in restricted or restricted[key] == index
                        self.assertEqual(parsed.getint('Patches', key), requested if supported else value)
                    self.assertEqual(parsed.getint('Patches', 'FastFaceTextureCreation'), value)
                    self.assertEqual(parsed.get('CustomSection', 'Custom'), 'keep')
                    before = config.read_bytes()
                    self.run_app('prepare', game)
                    self.run_app('rollback', game)
                    self.assertEqual(config.read_bytes(), before)

    def test_audio_tail_default_on_and_explicit_choices_preserved(self):
        for index in (0, 1, 2):
            with self.subTest(game=index):
                game, _ = self.fixture(index)
                proxy_dir = game / 'Birth3' if index == 2 else game
                proxy_dir.mkdir(exist_ok=True)
                config = proxy_dir / 'rebirths-patches.ini'
                # Fresh installs ship the enabled setting.
                self.run_app('prepare', game)
                parsed = configparser.ConfigParser()
                parsed.read(config, encoding='utf-8')
                self.assertEqual(parsed.getint('Patches', 'TrimSilentAudioTails'), 1)
                fresh_bytes = config.read_bytes()
                self.run_app('rollback', game)
                self.assertEqual(config.read_bytes(), fresh_bytes)
                # Missing-key upgrades retain the absent key and use the runtime
                # default; the independent catalog fixture checks that fallback.
                for value in (None, 0, 1):
                    with self.subTest(value=value):
                        before = b'[Patches]\r\nUncompressedAssets=0\r\n'
                        if value is not None:
                            before += f'TrimSilentAudioTails={value}\r\n'.encode('ascii')
                        config.write_bytes(before)
                        self.run_app('prepare', game)
                        parsed = configparser.ConfigParser()
                        parsed.read(config, encoding='utf-8')
                        self.assertEqual(parsed.getint('Patches', 'TrimSilentAudioTails', fallback=1),
                                         1 if value is None else value)
                        self.assertEqual(parsed.has_option('Patches', 'TrimSilentAudioTails'),
                                         value is not None)
                        self.run_app('rollback', game)
                        self.assertEqual(config.read_bytes(), before)

    def test_all_four_install_uninstall_and_semicolon(self):
        for index in range(4):
            with self.subTest(game=index):
                game, exe = self.fixture(index)
                before = digest(exe)
                self.run_app("prepare", game)
                proxy_dir = game / "Birth3" if index == 2 else game
                proxy = proxy_dir / "X3DAudio1_7.dll"
                config = proxy_dir / "rebirths-patches.ini"
                embedded = subprocess.check_output([str(APP), "embedded-hash"], text=True).strip()
                self.assertEqual(digest(proxy), embedded)
                text = config.read_text(encoding="utf-16" if config.read_bytes().startswith(b"\xff\xfe") else "utf-8")
                self.assertIn("UncompressedAssets=1", text)
                self.assertIn("FastTextureConversion=1", text)
                self.assertIn("SkipTutorials=1", text)
                self.assertIn("FastFaceTextureCreation=0", text)
                self.assertEqual(before, digest(exe))
                self.run_app("uninstall", game)
                self.assertFalse(proxy.exists())
                self.assertTrue(config.exists())
                self.assertTrue((game / "rebirths-speedrun-patch/cg24-v1/base.manifest").exists())

    def test_texture_default_migration_explicit_off_and_rollback(self):
        for index in range(4):
            with self.subTest(game=index):
                game, _ = self.fixture(index)
                proxy_dir = game / "Birth3" if index == 2 else game
                proxy_dir.mkdir(exist_ok=True)
                config = proxy_dir / "rebirths-patches.ini"
                # A pre-feature installation adopts the new default.
                config.write_text("[Patches]\nUncompressedAssets=0\n", encoding="utf-8")
                if index in (2, 3):
                    config.write_text("[Patches]\nUncompressedAssets=0\nFastFaceTextureCreation=1\n", encoding="utf-8")
                self.run_app("prepare", game)
                self.assertIn("FastTextureConversion=1", config.read_text(encoding="utf-8"))
                # An explicit off choice survives updates and rollback.
                config.write_text(config.read_text(encoding="utf-8").replace("FastTextureConversion=1",
                                                                          "FastTextureConversion=0"), encoding="utf-8")
                self.run_app("prepare", game)
                self.run_app("prepare", game)
                self.assertIn("FastTextureConversion=0", config.read_text(encoding="utf-8"))
                self.run_app("rollback", game)
                self.assertIn("FastTextureConversion=0", config.read_text(encoding="utf-8"))
                if index in (2, 3):
                    self.assertIn("FastFaceTextureCreation=1", config.read_text(encoding="utf-8"))

    def test_tutorial_missing_default_explicit_off_and_recovery(self):
        for index in range(4):
            with self.subTest(game=index):
                game, _ = self.fixture(index)
                proxy_dir = game / "Birth3" if index == 2 else game
                proxy_dir.mkdir(exist_ok=True)
                config = proxy_dir / "rebirths-patches.ini"
                config.write_text("[Patches]\nAdvAutoSkip=1\n[CustomSection]\nCustom=keep\n", encoding="utf-8")
                self.run_app("prepare", game)
                self.assertIn("SkipTutorials=1", config.read_text(encoding="utf-8"))
                config.write_text(config.read_text(encoding="utf-8").replace("SkipTutorials=1", "SkipTutorials=0"), encoding="utf-8")
                self.run_app("prepare", game)
                before = config.read_bytes()
                env = dict(os.environ, REBIRTHS_PREPARE_FAILPOINT="after-config")
                self.run_app("prepare", game, expected=1, env=env)
                self.assertEqual(config.read_bytes(), before)
                self.run_app("prepare", game)
                self.run_app("rollback", game)
                self.assertIn("SkipTutorials=0", config.read_text(encoding="utf-8"))
                self.assertIn("AdvAutoSkip=1", config.read_text(encoding="utf-8"))
                self.assertIn("Custom=keep", config.read_text(encoding="utf-8"))
                self.run_app("uninstall", game)
                self.assertEqual(config.read_bytes(), before)

    def test_update_without_state_preserves_settings_and_rollback(self):
        game, _ = self.fixture(0)
        self.run_app("prepare", game)
        config = game / "rebirths-patches.ini"
        with config.open("a", encoding="utf-8") as stream:
            stream.write("\n[CustomSection]\nCustom=keep\n")
        state = game / "rebirths-prepare-state.ini"
        state.unlink()
        self.run_app("prepare", game)
        self.assertIn("Custom=keep", config.read_text(encoding="utf-8"))
        self.assertNotIn("ProxySHA256", state.read_text(encoding="utf-8"))
        self.run_app("rollback", game)
        self.assertIn("Custom=keep", config.read_text(encoding="utf-8"))

    def test_stale_proxy_hash_state_is_not_carried_forward(self):
        game, _ = self.fixture(0)
        self.run_app("prepare", game)
        state = game / "rebirths-prepare-state.ini"
        with state.open("a", encoding="utf-8") as stream:
            stream.write("\nProxySHA256=stale\n")
        self.run_app("prepare", game)
        self.assertNotIn("ProxySHA256", state.read_text(encoding="utf-8"))
        self.run_app("rollback", game)
        self.assertNotIn("ProxySHA256", state.read_text(encoding="utf-8"))

    def test_unknown_proxy_is_preserved(self):
        game, _ = self.fixture(1)
        proxy = game / "X3DAudio1_7.dll"
        proxy.write_bytes(b"another mod")
        self.run_app("prepare", game, expected=1)
        self.assertEqual(proxy.read_bytes(), b"another mod")

    def test_legacy_rebirth3_loader_is_not_migrated(self):
        game, _ = self.fixture(2)
        (game / "steam_api_original.dll").write_bytes(b"legacy marker")
        self.run_app("prepare", game, expected=1)
        self.assertFalse((game / "Birth3/X3DAudio1_7.dll").exists())
        self.assertEqual((game / "steam_api_original.dll").read_bytes(), b"legacy marker")

    def test_known_old_proxy_without_state_updates_and_rolls_back(self):
        expected = "c9accd7ac76ab9cd804565840e70b6188bc75ad2a471e23b4b1dd1c871b3c452"
        candidates = [ROOT / "build/qualify/installed/rebirth1/previous-X3DAudio1_7.dll",
                      Path(r"F:\SteamLibrary\steamapps\common\Neptunia Rebirth1\X3DAudio1_7.dll")]
        old = next((p for p in candidates if p.is_file() and digest(p) == expected), None)
        if old is None:
            self.skipTest("Historical c9accd7a proxy bytes are unavailable locally")
        game, _ = self.fixture(0)
        proxy = game / "X3DAudio1_7.dll"
        shutil.copy2(old, proxy)
        self.run_app("prepare", game)
        self.assertNotEqual(digest(proxy), digest(old))
        self.run_app("rollback", game)
        self.assertEqual(digest(proxy), digest(old))

    def test_interrupted_publish_recovers_previous_proxy_and_settings(self):
        game, _ = self.fixture(0)
        self.run_app("prepare", game)
        files = [game / "X3DAudio1_7.dll", game / "rebirths-patches.ini",
                 game / "rebirths-prepare-state.ini"]
        before = [path.read_bytes() for path in files]
        for point in ["after-proxy", "after-config"]:
            env = dict(os.environ, REBIRTHS_PREPARE_FAILPOINT=point)
            self.run_app("prepare", game, expected=1, env=env)
            self.assertEqual([path.read_bytes() for path in files], before)

    def test_profile_switch_requires_separate_owned_destination(self):
        game, _ = self.fixture(0)
        self.run_app("prepare", game)
        half = game / "rebirths-speedrun-patch/cg24-v1"
        raw = game / "rebirths-speedrun-patch/assets"
        self.assertIn('"transform":"adv-cg-half24-box-v1"',
                      (half / "owner.json").read_text())
        self.run_app("prepare-raw", game, half, expected=1)
        self.run_app("prepare-raw", game, raw)
        self.assertNotIn('"transform"', (raw / "owner.json").read_text())
        self.run_app("prepare-adv-cg", game, half)
        self.assertTrue((half / "base.manifest").exists())
        self.assertTrue((raw / "base.manifest").exists())

    def test_settings_profile_defaults_without_owner(self):
        game, _ = self.fixture(0)
        self.assertIn("profile=3", self.run_app("settings", game).stdout)
        (game / "rebirths-patches.ini").write_text(
            "[UncompressedAssets]\nDirectory=prepared\n", encoding="utf-8")
        self.assertIn("profile=0", self.run_app("settings", game).stdout)

    def test_settings_profiles_ignore_json_serialization(self):
        game, _ = self.fixture(2)
        self.run_app("prepare", game)
        owner = game / "rebirths-speedrun-patch/cg24-v1/owner.json"
        original = json.loads(owner.read_text())
        recipes = {
            0: None,
            1: "rb3-ma123-half-box-v1",
            2: "rb3-ma106-107-123-half-box-v2",
            3: "adv-cg-half24-box-v1",
        }
        for profile, recipe in recipes.items():
            document = dict(original)
            document.pop("transform", None)
            if recipe is not None:
                document["transform"] = recipe
            compact = json.dumps(document, separators=(",", ":"))
            variants = [
                compact,
                json.dumps(document, indent=2),
                json.dumps(dict(reversed(list(document.items()))), indent=4),
                compact.replace('"transform"', '"transfor\\u006d"').replace(
                    "half", "ha\\u006cf"),
            ]
            for text in variants:
                with self.subTest(profile=profile, text=text):
                    owner.write_text(text, encoding="utf-8")
                    self.assertIn(f"profile={profile}",
                                  self.run_app("settings", game).stdout)

    def test_formatted_owner_settings_and_preparation_agree_all_games(self):
        for index in range(4):
            with self.subTest(game=index):
                game, _ = self.fixture(index)
                self.run_app("prepare", game)
                owner = game / "rebirths-speedrun-patch/cg24-v1/owner.json"
                document = json.loads(owner.read_text())
                owner.write_text(json.dumps(document, indent=2), encoding="utf-8")
                self.assertIn("profile=3", self.run_app("settings", game).stdout)
                # Before the fix this selects Raw and fails output ownership.
                self.run_app("prepare", game)
                self.assertEqual(json.loads(owner.read_text()), document)

    def test_invalid_owner_rejected_by_settings_and_preparation(self):
        game, _ = self.fixture(0)
        self.run_app("prepare", game)
        owner = game / "rebirths-speedrun-patch/cg24-v1/owner.json"
        original = json.loads(owner.read_text())
        compact = json.dumps(original, separators=(",", ":"))
        invalid = [
            b"", b"{", b"[]", b"null", compact.encode() + b" trailing",
            compact.replace('"format":1', '"format":01').encode(),
            compact.replace('"format":1', '"format":true').encode(),
            b"\v" + compact.encode(),
            compact.replace('"transform":', '"transform":null,"transform":').encode(),
            compact.replace("adv-cg-half24-box-v1", "adv-cg-\\ud800").encode(),
            compact.replace("adv-cg-half24-box-v1", "adv-cg-\xff").encode("latin-1"),
        ]
        for key, value in [
            ("transform", "unknown-recipe"), ("transform", None),
            ("transform", 3), ("transform", ""),
            ("transform", "rb3-ma123-half-box-v1"),
            ("game", 2), ("format", 2), ("backend", 2),
            ("game_directory", str(self.root / "other")),
            ("exclusions", []), ("extra", "adv-cg-half24-box-v1"),
        ]:
            invalid.append(json.dumps(dict(original, **{key: value})).encode())
        incomplete = dict(original)
        del incomplete["game"]
        invalid.append(json.dumps(incomplete).encode())
        for content in invalid:
            with self.subTest(content=content):
                owner.write_bytes(content)
                before = {p.relative_to(game): p.read_bytes()
                          for p in game.rglob("*") if p.is_file()}
                for action in ("settings", "prepare"):
                    run = self.run_app(action, game, expected=1)
                    self.assertIn("ownership metadata", run.stderr)
                after = {p.relative_to(game): p.read_bytes()
                         for p in game.rglob("*") if p.is_file()}
                self.assertEqual(after, before)

    def test_rb2_exclusion_schema_read_without_source_hashing(self):
        game, _ = self.fixture(1)
        self.run_app("prepare", game)
        owner = game / "rebirths-speedrun-patch/cg24-v1/owner.json"
        document = json.loads(owner.read_text())
        document["exclusions"] = [{
            "source": "data/GAME00001.pac",
            "sha256": "a2a8efac6f0eea5be4a5b03816ad543be06fb5ef11ebc049fa54ac85a13dba52",
            "ordinal": 553,
            "reason": "Native block 14 needs 77 bytes beyond its declared allocation; "
                      "full and partial native reads cannot share a deterministic "
                      "prepared result. See docs/rebirth2/uncompressed-loading/log.md.",
        }]
        owner.write_text(json.dumps(document, indent=2), encoding="utf-8")
        self.assertIn("profile=3", self.run_app("settings", game).stdout)
        # Settings read only ownership metadata; preparation must still enforce
        # actual source identity before accepting the declared exclusion.
        run = self.run_app("prepare", game, expected=1)
        self.assertIn("Output ownership mismatch", run.stderr)
        (game / "data/GAME00001.pac").write_bytes(b"unvalidated source")
        self.assertIn("profile=3", self.run_app("settings", game).stdout)
        self.assertIn("Unsupported Re;Birth2 GAME00001.pac version",
                      self.run_app("prepare", game, expected=1).stderr)
        document["exclusions"][0]["ordinal"] = 554
        owner.write_text(json.dumps(document), encoding="utf-8")
        self.assertIn("ownership metadata",
                      self.run_app("settings", game, expected=1).stderr)

    def test_optional_4gb_patch_and_separate_restore(self):
        for index in range(4):
            with self.subTest(game=index):
                game, exe = self.fixture(index)
                self.run_app("apply-4gb", game)
                self.assertEqual(digest(exe), NTCORE[index])
                self.run_app("restore-exe", game)
                self.assertEqual(digest(exe), BASELINES[index][3])

    def test_linked_library_parent_supports_installer_lifecycle(self):
        for index in range(4):
            with self.subTest(game=index):
                game, _ = self.fixture(index)
                library = self.root / ("library-" + str(index))
                junction(library, self.root)
                selected = library / game.name
                self.run_app("inspect", selected)
                self.run_app("prepare", selected, str(selected / "prepared") + os.sep)
                self.run_app("prepare", selected, str(selected / "prepared") + os.sep)
                self.run_app("prepare", selected)
                self.run_app("prepare", selected)
                self.run_app("rollback", selected)
                self.run_app("apply-4gb", selected)
                self.run_app("restore-exe", selected)
                self.run_app("uninstall", selected)

    def test_linked_backup_directory_is_refused(self):
        game, exe = self.fixture(0)
        outside = self.root / "outside-backups"
        outside.mkdir()
        junction(game / "rebirths-prepare-backups", outside)
        before = digest(exe)
        for action in ("prepare", "apply-4gb"):
            run = self.run_app(action, game, expected=1)
            self.assertIn("Reparse point refused", run.stderr)
        self.assertEqual(digest(exe), before)
        self.assertEqual(list(outside.iterdir()), [])
        self.assertFalse((game / "X3DAudio1_7.dll").exists())

    def test_unicode_semicolon_proxy_directory(self):
        game, _ = self.fixture(0)
        selected = game.with_name("Rebirth1;画像")
        game.rename(selected)
        self.run_app("prepare", selected)
        self.assertTrue((selected / "画像/X3DAudio1_7.dll").exists())
        self.run_app("uninstall", selected)
        self.assertFalse((selected / "画像/X3DAudio1_7.dll").exists())


if __name__ == "__main__":
    unittest.main()
