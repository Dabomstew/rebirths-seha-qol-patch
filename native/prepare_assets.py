"""Research CLI: inspect/prepare/verify all installed original PAC entries.

Requires 64-bit Python and native/build-asset-decoder.cmd for preparation.
No game execution or source mutation. Ctrl+C cancels before manifest publication.
"""
from __future__ import annotations
import argparse
import contextlib
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import time
from catalog_data import TARGETS, TARGET_CATALOG, target_for_id

from asset_format import (PACK_LIMIT, CHUNK, Decoder, archive_group, checked_path,
                          decode_manifest, digest_range, encode_manifest,
                          inspect_archive, require, ticks)



def json_bytes(value):
    return (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()


def atomic_write(path, data):
    temporary = path.with_name(path.name + '.tmp-' + str(os.getpid()))
    try:
        with temporary.open('xb') as f:
            f.write(data); f.flush(); os.fsync(f.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


@contextlib.contextmanager
def locked_source(path, exclusive=False):
    """Windows handle denies writing/deletion until the scope exits."""
    if os.name != 'nt':
        with path.open('rb') as f:
            yield f
        return
    import msvcrt
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                  ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
    kernel.CreateFileW.restype = ctypes.c_void_p
    handle = kernel.CreateFileW(str(path.absolute()), 0x80000000, 0 if exclusive else 1,
                                None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except BaseException:
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]; kernel.CloseHandle(handle); raise
    with os.fdopen(fd, 'rb') as f:
        yield f


def identify(root):
    for target in TARGET_CATALOG:
        path = root / target['executable']
        if path.is_file():
            require(hashlib.sha256(path.read_bytes()).hexdigest() == target['hashes']['baseline'],
                    'Unsupported executable identity (LAA qualification is pending)')
            return target['id']
    raise ValueError('Supported game executable not found')


def discover(root, game, scope):
    target_for_id(game)
    folders = ['data'] if scope == 'base' else (['DLC_EN', 'DLC_JP', 'DLC_CN'] if game == 3 else ['DLC'])
    found = []
    for name in folders:
        directory = checked_path(root, name)
        if not directory.exists():
            continue
        require(directory.is_dir(), 'Source root is not a directory')
        for current, directories, files in os.walk(directory, followlinks=False):
            for name2 in directories:
                checked_path(Path(current) / name2)
            for name2 in files:
                if Path(name2).suffix.lower() in ('.pac', '.cpk'):
                    p = checked_path(Path(current) / name2)
                    relative = p.relative_to(root).as_posix()
                    group = archive_group(relative) if p.suffix.lower() == '.pac' else relative[:-4].lower()
                    found.append((relative, group, int(p.suffix.lower() == '.pac')))
    found.sort(key=lambda item: item[0].lower())
    require(len({x[0].lower() for x in found}) == len(found), 'Case-colliding archive paths')
    return found


def collect(root, game, scope, stack, full_hash=True):
    listing = discover(root, game, scope)
    sources, archives = [], {}
    for relative, group, kind in listing:
        stream = stack.enter_context(locked_source(root / relative))
        stream.seek(0, 2); size = stream.tell()
        if kind:
            part, index_size, entries = inspect_archive(stream, relative)
            archives[len(sources)] = (stream, part, entries)
        else:
            require(size <= 64 * 1024 * 1024, 'CPK index exceeds bound')
            index_size = size
        index_hash = digest_range(stream, 0, index_size)
        full = digest_range(stream, 0, size) if full_hash else bytes(32)
        sources.append(dict(path=relative, group=group, kind=kind, size=size,
                            ticks=ticks(root / relative), index_size=index_size,
                            hash=full.hex(), index_hash=index_hash.hex()))
    cpk_groups = {s['group'] for s in sources if s['kind'] == 0}
    require(all(s['group'] in cpk_groups for s in sources if s['kind']), 'PAC namespace lacks its CPK index')
    return listing, sources, archives


def file_valid(root, record):
    path = checked_path(root, record['path'])
    if not path.is_file():
        return False
    with locked_source(path) as f:
        f.seek(0, 2)
        if f.tell() != record['storage_size']:
            return False
        return digest_range(f, record['offset'], record['size']).hex() == record['hash']


def batches(entries, limit):
    current, length = [], 20
    for e in entries:
        require(20 + 288 + e.size <= limit, 'Single asset exceeds generated PAC bound')
        if current and (length + 288 + e.size > limit or len(current) == 65536):
            yield current; current, length = [], 20
        current.append(e); length += 288 + e.size
    if current:
        yield current


def write_archive(root, scope, source, source_index, stream, part, entries, backend, decoder, limit):
    # Full source hash + original path give stable ownership across source-set ordering changes.
    identity = hashlib.sha256((source['path'] + source['hash'] + str(backend) + str(limit)).encode()).hexdigest()
    directory = checked_path(root, scope + '/' + identity)
    directory.mkdir(parents=True, exist_ok=True)
    journal = directory / 'complete.json'
    if journal.is_file():
        saved = json.loads(journal.read_text())
        require(saved['source'] == source and saved['backend'] == backend, 'Resume source mismatch')
        records = saved['files']
        require(len(records) == len(entries), 'Incomplete archive journal')
        for entry, record in zip(entries, records):
            require(record['id'] == (part << 16 | entry.ordinal) and record['metadata'] == entry.metadata.hex(),
                    'Resume identity mismatch')
            require(file_valid(root, record), 'Existing output differs; preserve it and choose a fresh output directory')
            record['source'] = source_index
        return records, len(records)
    records = []
    groups = list(batches(entries, limit)) if backend else [[entry] for entry in entries]
    for number, group in enumerate(groups):
        name = f'pack{number:05d}.pac' if backend else f'{group[0].ordinal:05d}.bin'
        final = directory / name
        temporary = directory / (name + '.tmp-' + str(os.getpid()))
        rows = []
        try:
            with temporary.open('xb') as output:
                offset = 0
                if backend:
                    output.write(b'DW_PACK\0' + struct.pack('<III', 0, len(group), number))
                    relative = 0
                    for index, entry in enumerate(group):
                        metadata = bytearray(entry.metadata)
                        struct.pack_into('<I', metadata, 4, index)
                        struct.pack_into('<IIII', metadata, 272, entry.size, entry.size, 0, relative)
                        output.write(metadata); relative += entry.size
                    offset = 20 + len(group) * 288
                for entry in group:
                    digest = hashlib.sha256(); size = 0
                    for chunk in decoder.decode(stream, entry):
                        output.write(chunk); digest.update(chunk); size += len(chunk)
                    require(size == entry.size, 'Decoded byte count mismatch')
                    rows.append(dict(source=source_index, id=part << 16 | entry.ordinal,
                                     path=final.relative_to(root).as_posix(), offset=offset, size=size,
                                     hash=digest.hexdigest(), metadata=entry.metadata.hex()))
                    offset += size
                require(not backend or output.tell() <= limit, 'Generated PAC exceeds cap')
                for row in rows:
                    row['storage_size'] = output.tell()
                output.flush(); os.fsync(output.fileno())
            # A pre-existing file is accepted only if byte-identical, never overwritten blindly.
            if final.exists():
                checked_path(final)
                with locked_source(final) as a, locked_source(temporary) as b:
                    size = temporary.stat().st_size
                    require(final.stat().st_size == size and digest_range(a, 0, size) == digest_range(b, 0, size),
                            'Conflicting output preserved: ' + str(final))
                temporary.unlink()
            else:
                os.rename(temporary, final)
            for row in rows:
                require(file_valid(root, row), 'Published output failed independent reread')
            records.extend(rows)
        finally:
            if temporary.exists():
                temporary.unlink()
    atomic_write(journal, json_bytes(dict(source=source, backend=backend, files=records)))
    return records, 0


def validate_exclusions(game_root, exclusions):
    """Explicit research exceptions are tied to exact original PAC bytes."""
    require(isinstance(exclusions, list) and len(exclusions) <= 1024, 'Invalid exclusion list')
    seen = set()
    for row in exclusions:
        require(set(row) == {'source', 'sha256', 'ordinal', 'reason'}, 'Invalid exclusion record')
        path = checked_path(game_root, row['source'])
        require(isinstance(row['ordinal'], int) and not isinstance(row['ordinal'], bool), 'Invalid excluded ordinal')
        require(isinstance(row['reason'], str) and 0 < len(row['reason']) <= 1024, 'Exclusion needs a reason')
        key = (row['source'], row['ordinal'])
        require(key not in seen, 'Duplicate exclusion'); seen.add(key)
        with locked_source(path) as stream:
            _, _, entries = inspect_archive(stream, path.name)
            require(0 <= row['ordinal'] < len(entries), 'Excluded entry not present')
            require(digest_range(stream, 0, path.stat().st_size).hex() == row['sha256'], 'Excluded source identity mismatch')
    return seen


def prepare(game_root, root, game, backend, decoder, limit=PACK_LIMIT, exclusions=None):
    target_for_id(game)
    require(308 <= limit <= PACK_LIMIT, 'Invalid pack limit')
    game_root = Path(os.path.abspath(game_root)); root = checked_path(root)
    require(root != game_root and root not in game_root.parents, 'Output cannot contain the game installation')
    # Refuse output inside any original source tree.
    for name in ('data', 'DLC', 'DLC_EN', 'DLC_JP', 'DLC_CN'):
        source_root = game_root / name
        require(root != source_root and source_root not in root.parents, 'Output overlaps original source tree')
    owner = root / 'owner.json'
    expected = dict(format=1, game=game, backend=backend, game_directory=str(game_root))
    exclusions = exclusions or []
    excluded = validate_exclusions(game_root, exclusions)
    discovered = {row[0] for scope in ('base', 'dlc') for row in discover(game_root, game, scope)}
    require(all(row['source'] in discovered for row in exclusions), 'Excluded source is outside native discovery')
    if exclusions:
        expected['exclusions'] = exclusions
    if root.exists():
        require(root.is_dir(), 'Output is not a directory')
        require(owner.is_file() or not any(root.iterdir()), 'Unowned nonempty output refused')
    else:
        root.mkdir(parents=True)
    if owner.exists():
        checked_path(owner)
        require(json.loads(owner.read_text()) == expected, 'Output ownership mismatch')
    else:
        atomic_write(owner, json_bytes(expected))
    # Held for both stages, preventing competing preparation into the same tree.
    with locked_source(owner, exclusive=True):
        totals = dict(files=0, bytes=0, reused=0, scopes=[], excluded=exclusions, full_coverage=not exclusions)
        for scope in ('base', 'dlc'):
            with contextlib.ExitStack() as stack:
                listing, sources, archives = collect(game_root, game, scope, stack)
                if not archives:
                    require(scope != 'base', 'No base PAC archives found')
                    continue  # Absent DLC produces no message and no new manifest.
                records = []
                for source_index, (stream, part, entries) in archives.items():
                    source = sources[source_index]
                    for exception in exclusions:
                        if exception['source'] == source['path']:
                            require(exception['sha256'] == source['hash'], 'Excluded source changed')
                    entries = [entry for entry in entries if (source['path'], entry.ordinal) not in excluded]
                    print(json.dumps(dict(stage='prepare', source=source['path'], entries=len(entries))), flush=True)
                    rows, reused = write_archive(root, scope, source, source_index, stream, part, entries, backend, decoder, limit)
                    records.extend(rows); totals['reused'] += reused
                require(discover(game_root, game, scope) == listing, 'Source set changed during preparation')
                for source in sources:
                    p = game_root / source['path']
                    require(p.stat().st_size == source['size'] and ticks(p) == source['ticks'], 'Source changed during preparation')
                data = encode_manifest(game, backend, sources, records)
                require(decode_manifest(data) == (game, backend, sources, records), 'Manifest roundtrip failure')
                atomic_write(root / (scope + '.manifest'), data)
                totals['files'] += len(records); totals['bytes'] += sum(r['size'] for r in records); totals['scopes'].append(scope)
        return totals


def verify(root, game_root, game):
    target_for_id(game)
    root = checked_path(root); total = dict(files=0, bytes=0, sources=0, source_entries=0)
    owner = checked_path(root, 'owner.json')
    exclusions = json.loads(owner.read_text()).get('exclusions', []) if owner.is_file() else []
    validate_exclusions(game_root, exclusions)
    total['excluded'] = exclusions
    for scope in ('base', 'dlc'):
        available = discover(game_root, game, scope)
        if scope == 'dlc' and not available:
            continue
        path = checked_path(root, scope + '.manifest')
        selected, backend, sources, files = decode_manifest(path.read_bytes())
        require(selected == game, 'Manifest game mismatch')
        with contextlib.ExitStack() as stack:
            _, current, archives = collect(game_root, game, scope, stack)
            require(current == sources, 'Source identity/set mismatch')
            for record in files:
                require(file_valid(root, record), 'Generated bytes mismatch: ' + record['path'])
            total['sources'] += len(sources); total['files'] += len(files); total['bytes'] += sum(f['size'] for f in files)
            total['source_entries'] += sum(len(rows) for _, _, rows in archives.values())
    total['full_coverage'] = total['files'] == total['source_entries'] and not exclusions
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['inspect', 'prepare', 'verify'])
    parser.add_argument('--game-directory', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--backend', choices=['loose', 'pac'], default='pac')
    parser.add_argument('--decoder', type=Path)
    parser.add_argument('--exclusions', type=Path, help='Research-only JSON exceptions bound to full PAC hashes; omitted entries keep native fallback')
    args = parser.parse_args()
    root = Path(os.path.abspath(args.game_directory)); game = identify(root)
    output = args.output or root / 'rebirths-speedrun-patch/assets'
    started = time.perf_counter()
    if args.operation == 'prepare':
        excluded = json.loads(args.exclusions.read_text(encoding='utf-8')) if args.exclusions else None
        result = prepare(root, output, game, int(args.backend == 'pac'), Decoder(args.decoder), exclusions=excluded)
    elif args.operation == 'verify':
        result = verify(output, root, game)
    else:
        result = dict(game=target_for_id(game)['name'], archives=[])
        for scope in ('base', 'dlc'):
            with contextlib.ExitStack() as stack:
                _, sources, archives = collect(root, game, scope, stack, full_hash=False)
                for index, (_, _, entries) in archives.items():
                    result['archives'].append(dict(path=sources[index]['path'], entries=len(entries),
                        packed=sources[index]['size'], decoded=sum(e.size for e in entries),
                        non_ascii=sum(any(c >= 128 for c in e.name) for e in entries)))
    result.update(operation=args.operation, elapsed=time.perf_counter() - started)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('Cancelled; completed source archives remain resumable.', file=sys.stderr); sys.exit(130)
    except (ValueError, OSError, KeyError) as error:
        print(str(error), file=sys.stderr); sys.exit(1)
