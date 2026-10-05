import contextlib
import hashlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import asset_format as fmt
import prepare_assets as prep
from test_prepare_assets import pac
from path_fixtures import junction

PROBE=Path(__file__).resolve().parents[2]/'build/asset-build/asset-store-probe.exe'

class NativeStoreTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.game=self.root/'game';self.out=self.root/'assets'
        pac(self.game/'data/GAME00000.pac',[(b'a',b'xyz',0),(b'b',b'Q'*31,1)])
        pac(self.game/'data/SYSTEM00000.pac',[(b'x',b'hello',0)])
        (self.game/'data/README').write_text('Unrelated extensionless file')
        self.decoder=fmt.Decoder()

    def prepare(self,backend=0):
        with contextlib.redirect_stdout(io.StringIO()):prep.prepare(self.game,self.out,0,backend,self.decoder,limit=380)

    def probe(self,expected=0):
        result=subprocess.run([str(PROBE),str(self.out),str(self.game),'0'],capture_output=True,text=True)
        self.assertEqual(result.returncode,expected,result.stderr)
        return result.stdout.strip()

    def test_interchange_both_backends_and_independent_handles(self):
        for backend in (0,1):
            self.out=self.root/str(backend);self.prepare(backend)
            self.assertEqual(self.probe(),f'3 0 {backend}')

    def test_namespace_change_keeps_unaffected_base(self):
        self.prepare();(self.game/'data/GAME.cpk').write_bytes(b'changed index')
        self.assertEqual(self.probe(),'1 1 0')

    def test_new_part_invalidates_namespace(self):
        self.prepare();pac(self.game/'data/GAME00001.pac',[(b'new',b'n',0)],1)
        self.assertEqual(self.probe(),'1 1 0')

    def test_optional_dlc_missing_removed_and_corrupt(self):
        pac(self.game/'DLC/100/DL0100000.pac',[(b'dlc',b'yes',0)])
        self.prepare();self.assertEqual(self.probe(),'4 0 0')
        path=self.game/'DLC/100/DL0100000.pac';data=path.read_bytes();path.unlink()
        self.assertEqual(self.probe(),'3 1 0')
        path.write_bytes(data)
        (self.out/'dlc.manifest').write_bytes(b'broken')
        self.assertEqual(self.probe(),'3 0 0')

    def test_bad_manifest_path_and_bounds(self):
        self.prepare();path=self.out/'base.manifest';original=path.read_bytes()
        for mutate in ('digest','path','range','kind'):
            game,backend,sources,files=fmt.decode_manifest(original)
            if mutate=='digest':data=original[:-1]+bytes([original[-1]^1])
            elif mutate=='path':
                # Same-length invalid path with valid manifest checksum.
                raw=original[:-32].replace(b'base/',b'../x/',1);data=raw+hashlib.sha256(raw).digest()
            else:
                if mutate=='range':files[0]['offset']=0x80000000
                else:sources[0]['kind']=1
                data=fmt.encode_manifest(game,backend,sources,files)
            path.write_bytes(data);self.probe(1)

    def test_generated_byte_corruption_detected(self):
        self.prepare();_,_,_,files=fmt.decode_manifest((self.out/'base.manifest').read_bytes())
        path=self.out/files[0]['path'];data=path.read_bytes();path.write_bytes(bytes([data[0]^1])+data[1:])
        self.probe(1)

    def test_unlisted_shortened_asset_is_rejected(self):
        self.prepare()
        manifest = self.out / "base.manifest"
        game, backend, sources, files = fmt.decode_manifest(manifest.read_bytes())
        files[0]["size"] -= 1
        manifest.write_bytes(fmt.encode_manifest(game, backend, sources, files))
        self.probe(1)

    def test_linked_library_parent_loads_and_verifies_assets(self):
        self.prepare(1)
        library = self.root / "library"
        junction(library, self.root)
        self.game = library / "game"
        self.out = library / "assets"
        self.assertEqual(self.probe(), "3 0 1")

    def test_linked_source_directory_is_refused(self):
        self.prepare(1)
        original = self.game / "data"
        moved = self.root / "moved-data"
        original.rename(moved)
        junction(original, moved)
        self.probe(1)

if __name__=='__main__':unittest.main()
