"""Verify the two shared face profiles against local immutable game baselines."""
import argparse
import hashlib
from compiled_profiles import profiles
from pe_image import PeImage
import struct
from pathlib import Path

GAMES = {
    'rebirth3': ('Rebirth3', 'NeptuniaReBirth3.exe', '455ab306a0e44b6e97af1ce2f7c6cf3b56c0352aaa1d62d24fffc2b11088751b'),
    'sega-hard-girls': ('Sega', 'Neptune VSSega Hard Girls.exe', 'af9550b0b5ab90810e31274066f13b3e1b224ef5960951a44d2acd53903523e5'),
}

def validate(game):
    profile, filename, digest = GAMES[game]
    root = Path(__file__).resolve().parents[3]
    image = (root / 'binaries' / game / filename).read_bytes()
    assert hashlib.sha256(image).hexdigest() == digest
    pe = PeImage(image)
    assert pe.image_base == 0x400000
    optional = pe.optional
    mapped, string = pe.mapped, pe.string
    body = profiles()[game]['face']
    assert body is not None and len(body['calls']) == 2
    surface, finish, iat = body['surface'], body['finish'], body['iat']
    for site in body['calls']:
        rva, expected = site['rva'], bytes(site['bytes'])
        assert len(expected) == 5
        actual = mapped(rva, 5)
        assert actual == expected and actual[0] == 0xe8
        assert pe.call_target(rva) == surface
    assert mapped(finish, 6) == b'\xff\x15' + struct.pack('<I', 0x400000 + iat)
    import_rva, import_size = struct.unpack_from('<II', image, optional+96+8)
    imports = []
    for offset in range(0, import_size, 20):
        lookup, _, _, name, slots = struct.unpack('<IIIII', mapped(import_rva+offset, 20))
        if not any((lookup, name, slots)): break
        for index in range(1024):
            entry = struct.unpack('<I', mapped((lookup or slots)+index*4, 4))[0]
            if not entry: break
            if slots+index*4 == iat:
                assert not entry & 0x80000000
                imports.append((string(name).lower(), string(entry+2)))
        else: raise AssertionError('import thunk bound exceeded')
    assert imports == [(b'opengl32.dll', b'glFinish')], imports
    reloc_rva, reloc_size = struct.unpack_from('<II', image, optional+96+5*8)
    table = mapped(reloc_rva, reloc_size)
    relocations, offset = [], 0
    while offset < len(table):
        page, size = struct.unpack_from('<II', table, offset)
        assert size >= 8 and offset+size <= len(table)
        for index in range(offset+8, offset+size, 2):
            entry = struct.unpack_from('<H', table, index)[0]
            if entry >> 12 == 3: relocations.append(page+(entry & 0xfff))
        offset += size
    assert finish + 2 in relocations
    print(f'{game}: exact face CALLs, surface target, relocated glFinish IAT contract verified')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', choices=GAMES, required=True)
    validate(parser.parse_args().game)
