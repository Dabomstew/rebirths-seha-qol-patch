"""Catalog reproducibility, independent identities and rejected schema mutations."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

NATIVE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(NATIVE))
import generate_catalog
import prepare_assets
from catalog_data import TARGET_CATALOG, target_for_id
from test_prepare_game_native import BASELINES, NTCORE


class CatalogTest(unittest.TestCase):
    def test_outputs_are_current(self):
        subprocess.run([sys.executable, str(NATIVE / 'generate_catalog.py'), '--check'], check=True)

    def test_target_identities_are_independent(self):
        for i, (name, folder, exe, expected) in enumerate(BASELINES):
            target = target_for_id(i)
            self.assertEqual(target['id'], i)
            self.assertEqual(target['name'], name if i != 3 else 'neptune-vs-sega-hard-girls')
            self.assertEqual(target['steam_folder'], folder)
            self.assertEqual(target['executable'], exe)
            self.assertEqual(target['hashes']['baseline'], expected)
            self.assertEqual(target['hashes']['ntcore'], NTCORE[i])
        self.assertEqual(len(TARGET_CATALOG), 4)

    def test_invalid_python_ids(self):
        for value in (-1, 4, 2**32, True, '0', None):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'Unsupported game ID'):
                target_for_id(value)

    def test_asset_apis_reject_invalid_ids_before_writes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for value in (-1, 4, 2**32, True, '0', None):
                for action in ('discover', 'prepare', 'verify'):
                    with self.subTest(value=value, action=action), self.assertRaisesRegex(ValueError, 'Unsupported game ID'):
                        if action == 'discover':
                            prepare_assets.discover(root / 'game', value, 'base')
                        elif action == 'prepare':
                            prepare_assets.prepare(root / 'game', root / 'output', value, 1, None)
                        else:
                            prepare_assets.verify(root / 'output', root / 'game', value)
                self.assertEqual(list(root.iterdir()), [])

    def test_order_does_not_define_serialized_ids(self):
        data = json.loads((NATIVE / 'catalog.json').read_text())
        original = generate_catalog.outputs(data)
        data['targets'].reverse()
        self.assertEqual(generate_catalog.outputs(data), original)

    def test_stale_output_is_rejected_without_writes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'generate_catalog.py').write_bytes((NATIVE / 'generate_catalog.py').read_bytes())
            (root / 'catalog.json').write_bytes((NATIVE / 'catalog.json').read_bytes())
            for path, content in generate_catalog.outputs(json.loads((NATIVE / 'catalog.json').read_text())).items():
                output = root / path
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_text(content, encoding='utf-8', newline='\n')
            damaged = root / 'include/game_ids.hpp'
            damaged.write_text('stale sentinel', encoding='utf-8')
            run = subprocess.run([sys.executable, str(root / 'generate_catalog.py'), '--check'],
                                 text=True, capture_output=True)
            self.assertEqual(run.returncode, 1)
            self.assertIn('include/game_ids.hpp', run.stderr)
            self.assertEqual(damaged.read_text(), 'stale sentinel')

    def test_invalid_catalog_mutations(self):
        data = json.loads((NATIVE / 'catalog.json').read_text())
        def reject(mutator):
            changed = copy.deepcopy(data)
            mutator(changed)
            with self.assertRaises(ValueError):
                generate_catalog.outputs(changed)
        mutations = [
            lambda d: d.update(version=2),
            lambda d: d.update(version=True),
            lambda d: d['targets'][0].update(id=3),
            lambda d: d['targets'][0].update(id=True),
            lambda d: d['targets'][0].update(name=d['targets'][1]['name']),
            lambda d: d['targets'][0]['hashes'].update(baseline='invalid'),
            lambda d: d['targets'][0]['hashes'].update(baseline=d['targets'][1]['hashes']['baseline']),
            lambda d: d['targets'][0].update(text_end=0),
            lambda d: d['targets'][0].update(local_record='../outside.json'),
            lambda d: d['targets'][0].update(steam_folder='../outside'),
            lambda d: d['features'][0].update(key=d['features'][1]['key']),
            lambda d: d['features'][0].update(support=['UnvalidatedGame']),
            lambda d: d['features'][0].update(support=['Rebirth1', 'Rebirth1']),
            lambda d: d['features'][0].update(runtime_default=2),
            lambda d: d['features'][0].update(fresh_default=True),
            lambda d: d['features'][0].update(preparer_policy='replace-everything'),
            lambda d: d['features'][0].update(member=None),
            lambda d: d['features'][1].update(member='uncompressedAssets'),
        ]
        for i, mutation in enumerate(mutations):
            with self.subTest(mutation=i):
                reject(mutation)


if __name__ == '__main__':
    unittest.main()
