"""Native confirmed extraction on isolated four-game baselines; never installed games."""
import configparser
from pathlib import Path
import struct
import unittest

from test_prepare_game_native import PreparerFixture
from test_prepare_assets import pac


def raw_index(path, names):
    records = bytearray()
    for name, identifier in names:
        records += name + bytes(260-len(name)) + struct.pack('<I', identifier)
    buckets = bytearray(b'\xff\xff\xff\xff\0\0\0\0' * 65536)
    struct.pack_into('<II', buckets, 0, 0, len(names))
    data = b'DW_PACK\0' + struct.pack('<II', 0, len(names)) + buckets + records
    path.write_bytes(struct.pack('<IIII', 0, len(data), len(data), 0) + data)


class Loose(unittest.TestCase):
    setUp = PreparerFixture.setUp
    fixture = PreparerFixture.fixture
    run_app = PreparerFixture.run_app
    def ini(self, game, index, root='loosefiles'):
        proxy = game/'Birth3' if index == 2 else game
        proxy.mkdir(exist_ok=True)
        config = proxy/'rebirths-patches.ini'
        config.write_text('[Patches]\nLooseFiles=0\nLooseFileDirectory='+str(root)+
                          '\nUncompressedAssets=0\n[UncompressedAssets]\nDirectory=prepared\n'
                          '[Personal]\nKeep=unchanged\n', encoding='utf-16' if any(ord(c)>127 for c in str(root)) else 'utf-8')
        return config

    def test_all_four_names_dlc_raw_and_compressed_default_root(self):
        for index in range(4):
            game, exe = self.fixture(index)
            config = self.ini(game, index)
            pac(game/'data/SYSTEM00000.pac', [(b'database/stitem.gbin', b'ABC', 0),
                                            (b'database/empty.gbin', b'', 0),
                                            (b'picture/test.tid', b'Z'*91, 1)])
            dlc = 'DLC_JP/001/DL01' if index == 3 else 'DLC/001/DL01'
            pac(game/(dlc+'00000.pac'), [(b'sub/addition.bin', b'DLC', 0)])
            original = {p:p.read_bytes() for p in game.rglob('*') if p.suffix in ('.pac','.cpk','.exe')}
            self.run_app('extract-no-confirm', game)
            self.assertFalse((game/'loosefiles').exists())
            self.assertIn('LooseFiles=0', config.read_text())
            result = self.run_app('extract-ui', game)
            root = game/'loosefiles'
            self.assertEqual((root/'data/SYSTEM/database/stitem.gbin').read_bytes(), b'ABC')
            self.assertEqual((root/'data/SYSTEM/database/empty.gbin').read_bytes(), b'')
            self.assertEqual((root/'data/SYSTEM/picture/test.tid').read_bytes(), b'Z'*91)
            self.assertEqual((root/dlc/'sub/addition.bin').read_bytes(), b'DLC')
            parsed = configparser.ConfigParser();parsed.read(config)
            self.assertEqual(parsed['Patches']['LooseFiles'], '1')
            self.assertEqual(parsed['Patches']['LooseFileDirectory'], 'loosefiles')
            self.assertEqual(parsed['Patches']['UncompressedAssets'], '0')
            self.assertEqual(parsed['Personal']['Keep'], 'unchanged')
            self.assertTrue((config.parent/'X3DAudio1_7.dll').exists())
            for p, data in original.items():self.assertEqual(p.read_bytes(), data)

    def test_existing_modified_and_empty_files_are_kept_and_resume_is_non_destructive(self):
        game, _ = self.fixture(0)
        config = self.ini(game, 0, 'custom mods')
        pac(game/'data/GAME00000.pac', [(b'a', b'ABC', 0), (b'b', b'B'*7, 1)])
        dest = game/'custom mods/data/GAME';dest.mkdir(parents=True)
        (dest/'a').write_bytes(b'custom mod, different size and bytes')
        (dest/'b').write_bytes(b'')
        result = self.run_app('extract', game)
        self.assertIn('kept=2', result.stdout)
        self.assertEqual((dest/'a').read_bytes(), b'custom mod, different size and bytes')
        self.assertEqual((dest/'b').read_bytes(), b'')
        self.run_app('extract', game)
        self.assertIn('LooseFileDirectory=custom mods', config.read_text())

    def test_absolute_unicode_root_and_missing_default_directory_key(self):
        for index in (0, 2):
            game, _ = self.fixture(index)
            dest = self.root/'outside loose \u65e5\u672c'
            config = self.ini(game, index, dest)
            self.run_app('extract', game)
            self.assertEqual((dest/'data/GAME/sample').read_bytes(), b'data')
            # Missing key uses exactly the runtime default, relative to the EXE.
            self.ini(game, index)
            config.write_text(config.read_text().replace('LooseFileDirectory=loosefiles\n',''))
            self.run_app('extract', game)
            self.assertEqual((game/'loosefiles/data/GAME/sample').read_bytes(), b'data')

    def test_cancel_failure_and_stale_config_do_not_enable_loading(self):
        game, _ = self.fixture(0)
        config = self.ini(game, 0)
        pac(game/'data/GAME00000.pac', [(b'a', b'A', 0), (b'b', b'B', 0), (b'c', b'C', 0)])
        before = config.read_bytes()
        self.run_app('extract-stale', game, expected=1)
        self.assertFalse((game/'loosefiles').exists())
        self.run_app('extract-cancel', game, expected=1)
        self.assertEqual(config.read_bytes(), before)
        self.assertEqual((game/'loosefiles/data/GAME/a').read_bytes(), b'A')
        self.assertFalse(list((game/'loosefiles').rglob('*.extract-*')))
        self.run_app('extract', game)
        self.assertIn('LooseFiles=1', config.read_text())

    def test_duplicate_names_follow_native_bucket_order(self):
        game, _ = self.fixture(3);self.ini(game, 3)
        pac(game/'data/GAME00000.pac', [(b'dup', b'FIRST', 0)])
        pac(game/'data/GAME00001.pac', [(b'dup', b'SECOND', 0)], 1)
        raw_index(game/'data/GAME.cpk', [(b'dup', 0x10000), (b'dup', 0)])
        self.run_app('extract', game)
        self.assertEqual((game/'loosefiles/data/GAME/dup').read_bytes(), b'SECOND')

    def test_corrupt_duplicate_index_is_refused_before_writes(self):
        game, _ = self.fixture(3);config = self.ini(game, 3)
        pac(game/'data/GAME00000.pac', [(b'dup', b'FIRST', 0)])
        pac(game/'data/GAME00001.pac', [(b'dup', b'SECOND', 0)], 1)
        before = config.read_bytes()
        self.run_app('extract', game, expected=1)
        self.assertFalse((game/'loosefiles').exists())
        self.assertEqual(config.read_bytes(), before)

    def test_traversal_and_source_overlap_are_refused(self):
        game, _ = self.fixture(0)
        for name in (b'../escape', b'C:/escape', b'x:stream'):
            config = self.ini(game, 0)
            pac(game/'data/GAME00000.pac', [(name, b'X', 0)])
            before = config.read_bytes()
            self.run_app('extract', game, expected=1)
            self.assertEqual(config.read_bytes(), before)
            self.assertFalse((game/'loosefiles').exists())
        config = self.ini(game, 0, 'data')
        self.run_app('extract', game, expected=1)
        self.assertIn('LooseFiles=0', config.read_text())

    def test_decode_and_install_failures_keep_settings_and_complete_files(self):
        game, _ = self.fixture(0);config = self.ini(game, 0)
        pac(game/'data/GAME00000.pac', [(b'a', b'A', 0), (b'b', b'B'*100, 1)])
        archive = game/'data/GAME00000.pac';data = bytearray(archive.read_bytes());relative = struct.unpack_from('<I', data, 20+288+284)[0];struct.pack_into('<I', data, 20+2*288+relative, 0);archive.write_bytes(data)
        before = config.read_bytes();self.run_app('extract', game, expected=1)
        self.assertEqual(config.read_bytes(), before)
        self.assertEqual((game/'loosefiles/data/GAME/a').read_bytes(), b'A')
        self.assertFalse((game/'loosefiles/data/GAME/b').exists())
        self.assertFalse(list((game/'loosefiles').rglob('*.extract-*')))
        pac(archive, [(b'a', b'A', 0)])
        self.run_app('extract', game, expected=1, env={'REBIRTHS_PREPARE_FAILPOINT':'after-config'})
        self.assertEqual(config.read_bytes(), before)

# Reuse fixture helpers without discovering the unrelated base test suite twice.
del PreparerFixture
