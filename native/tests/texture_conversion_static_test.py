"""Check game-specific native profiles against local immutable PE baselines."""
import argparse
import hashlib
import os
from compiled_profiles import profiles
from pe_image import PeImage
import struct
from pathlib import Path

HASHES = {
    'rebirth1': '0a6fdae55681019c9612f2da5e59c7539a2def22f66546695b8e9978ca88ab71',
    'rebirth2': 'f0b0b9fe40a26a001d0ac87702d51844f19900cfabe3d38680b885cc607c2fb2',
    'rebirth3': '455ab306a0e44b6e97af1ce2f7c6cf3b56c0352aaa1d62d24fffc2b11088751b',
    'sega-hard-girls': 'af9550b0b5ab90810e31274066f13b3e1b224ef5960951a44d2acd53903523e5',
}

def validate(game_id):
    number = game_id[-1]
    sega = game_id == 'sega-hard-girls'
    executable = 'Neptune VSSega Hard Girls.exe' if sega else f'NeptuniaReBirth{number}.exe'
    game = Path(os.environ.get(game_id.upper() + '_GAME_EXE',
                f'binaries/{game_id}/{executable}'))
    if not game.is_absolute():
        game = Path(__file__).resolve().parents[3] / game
    data = game.read_bytes()
    assert hashlib.sha256(data).hexdigest() == HASHES[game_id]
    pe = PeImage(data)
    assert pe.image_base == 0x400000
    def mapped(rva, size): return pe.mapped(rva, size, executable=True)
    profile = profiles()[game_id]['texture']
    assert profile['game'] == {'rebirth1': 0, 'rebirth2': 1, 'rebirth3': 2, 'sega-hard-girls': 3}[game_id]
    cookie = profile['cookie']
    assert len(profile['helpers']) == 5 and len(profile['calls']) == 3
    entries = [(entry['rva'], bytes(entry['bytes'])) for entry in profile['helpers'] + profile['calls']]
    assert len(entries) == 8
    for i, (rva, expected) in enumerate(entries[:5]):
        assert len(expected) == 16
        if i == 0:
            expected = expected[:7] + struct.pack('<I', 0x400000+cookie) + expected[11:]
        assert mapped(rva, 16) == expected, hex(rva)
    for (site, expected), (target, _) in zip(entries[5:], entries[:3]):
        actual = mapped(site, 5)
        assert actual == expected and actual[0] == 0xe8
        assert pe.call_target(site) == target
    print(f'{game_id} texture helpers and CALL contracts OK: {game}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', choices=HASHES, required=True)
    validate(parser.parse_args().game)
