import contextlib
import hashlib
import io
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import asset_format as fmt
import prepare_assets as prep


def leaf(symbol):
    bits = '0' + format(symbol, '08b')
    bits += '0' * (-len(bits) % 8)
    return int(bits, 2).to_bytes(len(bits) // 8, 'big')


def pac(path, rows, part=0):
    """Rows are opaque name, raw data, compression flag; compressed fixtures repeat a symbol."""
    table = bytearray(); data = bytearray()
    for i, (name, payload, flag) in enumerate(rows):
        encoded = payload
        if flag:
            block = leaf(payload[0])
            encoded = struct.pack('<IIIIIII', 0x1234, 1, 131072, 28, len(payload), len(block), 0) + block
        entry = bytearray(288); struct.pack_into('<II', entry, 0, 99, i)
        entry[8:8+len(name)] = name
        struct.pack_into('<IIII', entry, 272, len(encoded), len(payload), flag, len(data))
        table.extend(entry); data.extend(encoded)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'DW_PACK\0' + struct.pack('<III', 0, len(rows), part) + table + data)
    path.with_name(path.name[:-9] + '.cpk').write_bytes(b'synthetic index')


class FormatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.decoder = fmt.Decoder()

    def test_huffman(self):
        self.assertEqual(self.decoder.block(leaf(0x83), 17), b'\x83' * 17)
        bits = '1' + '0' + format(65, '08b') + '0' + format(66, '08b') + '0110'
        bits += '0' * (-len(bits) % 8)
        self.assertEqual(self.decoder.block(int(bits, 2).to_bytes(len(bits)//8, 'big'), 4), b'ABBA')
        for bad in (b'', b'\xff' * 1000):
            with self.assertRaises(ValueError): self.decoder.block(bad, 30)
        with self.assertRaises(ValueError): self.decoder.block(b'\0', fmt.BLOCK_LIMIT+1)

    def test_paths(self):
        for name in ('../x', '/x', 'a//b', 'a/CON.txt', 'a./b', 'a:b', 'x\\y', 'a/..', 'x\0y'):
            with self.subTest(name=name), self.assertRaises(ValueError): fmt.safe_relative(name)
        self.assertEqual(fmt.archive_group('DLC/ABC/DL0100000.pac'), 'dlc/abc/dl01')

    def test_single_native_zero_sentinel_only_after_complete_tree(self):
        # 19 tree bits leave five payload bits in three bytes. Exactly eight
        # additional zero bits are native EOF behavior, not unlimited padding.
        bits = '1' + '0' + format(65, '08b') + '0' + format(66, '08b') + '11111'
        encoded = int(bits, 2).to_bytes(3, 'big')
        self.assertEqual(self.decoder.block(encoded, 13), b'B'*5 + b'A'*8)
        with self.assertRaises(ValueError): self.decoder.block(encoded, 14)
        with self.assertRaises(ValueError): self.decoder.block(b'\0', 1)

    def test_raw_and_malformed(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'GAME00000.pac'; pac(p, [(b'a', b'abc', 0)])
            with p.open('rb') as f:
                _, _, entries=fmt.inspect_archive(f,p.name)
                self.assertEqual(b''.join(self.decoder.decode(f,entries[0])),b'abc')
            data=p.read_bytes()
            for bad in (data[:10], data[:-1], b'BAD_PACK'+data[8:]):
                with self.assertRaises(ValueError): fmt.inspect_archive(io.BytesIO(bad),p.name)
            with self.assertRaises(ValueError): fmt.inspect_archive(io.BytesIO(data),'GAME00001.pac')


class PreparationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name); self.game=self.root/'game'; self.game.mkdir()
        self.decoder=fmt.Decoder()
        self.rows=[(b'same', b'abc',0),(b'same',b'Z'*31,1),(b'bad/\x81\x5c.tid',b'xyz',0)]
        pac(self.game/'data/GAME00000.pac',self.rows)

    def run_prepare(self,backend,output=None,decoder=None):
        with contextlib.redirect_stdout(io.StringIO()):
            return prep.prepare(self.game,output or self.root/str(backend),0,backend,decoder or self.decoder,limit=380)

    def test_both_backends_split_duplicate_names_and_resume(self):
        for backend in (0,1):
            with self.subTest(backend=backend):
                result=self.run_prepare(backend);self.assertEqual(result['files'],3)
                out=self.root/str(backend)
                g,b,s,files=fmt.decode_manifest((out/'base.manifest').read_bytes())
                self.assertEqual([f['id'] for f in files],[0,1,2])
                self.assertEqual([bytes.fromhex(f['metadata'])[8:268].split(b'\0')[0] for f in files],[r[0] for r in self.rows])
                self.assertEqual(prep.verify(out,self.game,0)['files'],3)
                self.assertEqual(self.run_prepare(backend)['reused'],3)
                if backend:
                    self.assertEqual(len(set(f['path'] for f in files)),3)
                    for f in files:
                        p=out/f['path']
                        with p.open('rb') as stream:
                            _,_,entries=fmt.inspect_archive(stream,p.name)
                            self.assertTrue(all(e.compression==0 for e in entries))
                            self.assertLessEqual(p.stat().st_size,380)

    def test_corruption_and_unrelated_preservation(self):
        self.run_prepare(0);out=self.root/'0'
        _,_,_,files=fmt.decode_manifest((out/'base.manifest').read_bytes())
        victim=out/files[0]['path'];victim.write_bytes(b'BAD')
        with self.assertRaises(ValueError): self.run_prepare(0)
        self.assertEqual(victim.read_bytes(),b'BAD')
        with self.assertRaises(ValueError): prep.verify(out,self.game,0)
        unrelated=self.root/'unrelated';unrelated.mkdir();(unrelated/'user.txt').write_text('keep')
        with self.assertRaises(ValueError): self.run_prepare(0,unrelated)
        self.assertEqual((unrelated/'user.txt').read_text(),'keep')

    def test_manifest_tamper_and_source_change(self):
        self.run_prepare(1);out=self.root/'1';data=(out/'base.manifest').read_bytes()
        with self.assertRaises(ValueError): fmt.decode_manifest(data[:-1]+bytes([data[-1]^1]))
        (self.game/'data/GAME.cpk').write_bytes(b'changed')
        with self.assertRaises(ValueError): prep.verify(out,self.game,0)

    def test_optional_dlc_removed_reappeared_and_added(self):
        self.run_prepare(0);out=self.root/'0';self.assertFalse((out/'dlc.manifest').exists())
        dlc=self.game/'DLC/X/X00000.pac';pac(dlc,[(b'dlc',b'DLC',0)])
        self.assertEqual(self.run_prepare(0)['files'],4)
        self.assertEqual(prep.verify(out,self.game,0)['files'],4)
        dlc.rename(dlc.with_suffix('.removed'));cpk=dlc.with_name('X.cpk');cpk.rename(cpk.with_suffix('.removed'))
        self.assertEqual(prep.verify(out,self.game,0)['files'],3)
        dlc.with_suffix('.removed').rename(dlc);cpk.with_suffix('.removed').rename(cpk)
        self.assertEqual(prep.verify(out,self.game,0)['files'],4)
        pac(self.game/'data/OTHER00000.pac',[(b'new',b'new',0)])
        with self.assertRaises(ValueError): prep.verify(out,self.game,0)

    def test_cancellation_and_resume(self):
        class Cancel:
            def decode(_,stream,entry):
                raise KeyboardInterrupt()
        with self.assertRaises(KeyboardInterrupt): self.run_prepare(1,decoder=Cancel())
        out=self.root/'1';self.assertFalse((out/'base.manifest').exists())
        self.assertFalse(list(out.rglob('*.tmp-*')))
        self.assertEqual(self.run_prepare(1)['files'],3)

    def test_source_write_lock_and_output_overlap(self):
        if os.name=='nt':
            p=self.game/'data/GAME00000.pac'
            with prep.locked_source(p):
                with self.assertRaises(OSError): p.open('wb')
        with self.assertRaises(ValueError): self.run_prepare(0,self.game/'data/generated')
        with self.assertRaises(ValueError): self.run_prepare(0,self.game)

    def test_explicit_exclusion_is_hash_bound_and_never_claims_full_coverage(self):
        source=self.game/'data/GAME00000.pac';original=source.read_bytes()
        exception=dict(source='data/GAME00000.pac',sha256=hashlib.sha256(original).hexdigest(),ordinal=1,reason='Native output is not deterministic')
        output=self.root/'excluded'
        with contextlib.redirect_stdout(io.StringIO()):
            result=prep.prepare(self.game,output,0,1,self.decoder,exclusions=[exception])
        self.assertEqual(result['files'],2);self.assertFalse(result['full_coverage'])
        verified=prep.verify(output,self.game,0)
        self.assertEqual(verified['source_entries'],3);self.assertFalse(verified['full_coverage'])
        _,_,_,rows=fmt.decode_manifest((output/'base.manifest').read_bytes())
        self.assertEqual([r['id'] for r in rows],[0,2])
        with self.assertRaises(ValueError):prep.prepare(self.game,output,0,1,self.decoder)
        exception['sha256']='0'*64
        with self.assertRaises(ValueError):prep.prepare(self.game,self.root/'wrong',0,1,self.decoder,exclusions=[exception])
        self.assertEqual(source.read_bytes(),original)


if __name__=='__main__':unittest.main()
