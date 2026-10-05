"""Shared-shell adapter/settings-only tests on isolated copied baselines; no game launch."""
import configparser
from pathlib import Path
import unittest
from test_prepare_game_native import PreparerFixture, digest


class Ui(PreparerFixture):
    def test_ui_feature_support_defaults_and_groups(self):
        expected = [
            {'FastTextureConversion', 'UncompressedAssets', 'AdvFastForward', 'AdvAutoSkip', 'SkipTutorials', 'downscale'},
            {'FastTextureConversion', 'UncompressedAssets', 'AdvFastForward', 'AdvAutoSkip', 'SkipTutorials', 'SkipChapterIntros', 'downscale'},
            {'FastTextureConversion', 'UncompressedAssets', 'AdvFastForward', 'AdvAutoSkip', 'SkipTutorials', 'BattleLoadDelaySkip', 'NepstationSkip', 'downscale'},
            {'FastTextureConversion', 'UncompressedAssets', 'AdvFastForward', 'AdvAutoSkip', 'SkipTutorials', 'DungeonMovementFix', 'downscale'},
        ]
        for index in range(4):
            game, _ = self.fixture(index)
            rows = self.run_app('ui-features', game).stdout.splitlines()
            values = {r.split('=')[0]: int(r.split('=')[1].split()[0]) for r in rows}
            self.assertEqual(set(values), expected[index])
            for key in ('FastTextureConversion', 'UncompressedAssets', 'SkipTutorials', 'downscale'):
                self.assertEqual(values[key], 1)
            self.assertEqual(values['AdvAutoSkip'], 0)

    def test_install_does_not_prepare_or_read_archives(self):
        for index in range(4):
            game, exe = self.fixture(index)
            original = digest(exe)
            archive = game / 'data/GAME00000.pac'
            archive.write_bytes(b'Invalid archive must never be scanned by install')
            destination = self.root / ('missing-assets-' + str(index))
            self.run_app('install', game, destination)
            self.assertFalse(destination.exists())
            self.assertEqual(digest(exe), original)
            proxy = game / 'Birth3' if index == 2 else game
            self.assertTrue((proxy / 'X3DAudio1_7.dll').is_file())
            self.assertEqual(archive.read_bytes(), b'Invalid archive must never be scanned by install')

    def test_stale_config_is_refused_without_install_writes(self):
        game, _ = self.fixture(0)
        self.run_app('install', game)
        before = {p.relative_to(game): p.read_bytes() for p in game.rglob('*') if p.is_file()}
        for action in ('install-stale', 'install-absent-stale'):
            run = self.run_app(action, game, expected=1)
            self.assertIn('Settings changed on disk', run.stderr)
            self.assertEqual(before, {p.relative_to(game): p.read_bytes() for p in game.rglob('*') if p.is_file()})

    def test_unknown_proxy_refused_before_preparation(self):
        game, _ = self.fixture(0)
        (game / 'X3DAudio1_7.dll').write_bytes(b'other mod')
        destination = self.root / 'must-not-create'
        self.run_app('prepare', game, destination, expected=1)
        self.assertFalse(destination.exists())

    def test_install_preserves_advanced_and_unsupported_settings(self):
        for index in range(4):
            game, _ = self.fixture(index)
            self.run_app('install', game)
            proxy = game / 'Birth3' if index == 2 else game
            config = proxy / 'rebirths-patches.ini'
            config.write_text('[Patches]\nFastFaceTextureCreation=1\nNepstationSkip=1\n[Personal]\nKeep=yes\n', encoding='utf-8')
            self.run_app('install-off', game)
            parsed = configparser.ConfigParser(); parsed.read(config, encoding='utf-8')
            self.assertEqual(parsed['Personal']['Keep'], 'yes')
            self.assertEqual(parsed['Patches']['FastFaceTextureCreation'], '1')
            if index != 2: self.assertEqual(parsed['Patches']['NepstationSkip'], '1')


if __name__ == '__main__': unittest.main()
