"""Lossless DW_PACK inspection, bounded decoding and native manifest interchange.

Logical names are opaque bytes. Native CPK resolution, never a Python filename
normalizer, selects the part/entry used by the runtime adapter.
"""
from __future__ import annotations
import ctypes
import hashlib
import io
import os
import re
import struct
from dataclasses import dataclass
from pathlib import Path

PACK_LIMIT = 0x60000000
BLOCK_LIMIT = 16 * 1024 * 1024
MAGIC = b'RBAST001'
CHUNK = 1024 * 1024


def require(condition, text):
    if not condition:
        raise ValueError(text)


def read_at(stream, offset, size):
    require(offset >= 0 and size >= 0, 'Negative range')
    stream.seek(offset)
    value = stream.read(size)
    require(len(value) == size, 'Truncated source')
    return value


def safe_relative(name):
    require(isinstance(name, str) and 0 < len(name) < 4096, 'Invalid relative path')
    require('\\' not in name and '\0' not in name, 'Noncanonical relative path')
    for part in name.split('/'):
        require(part not in ('', '.', '..') and not part.endswith(('.', ' ')), 'Unsafe component')
        require(all(32 <= ord(c) < 127 and c not in ':*?"<>|' for c in part), 'Unsafe path character')
        stem = part.split('.')[0].lower()
        require(stem not in ('con', 'prn', 'aux', 'nul') and not re.fullmatch(r'(com|lpt)[1-9]', stem), 'Reserved component')
    return name


def checked_path(root, relative=''):
    root = Path(os.path.abspath(root))
    path = root / safe_relative(relative) if relative else root
    # Check every existing ancestor, including the supplied output root.
    for component in [path, *path.parents]:
        if component.exists() or component.is_symlink():
            st = component.lstat()
            require(not component.is_symlink() and not getattr(st, 'st_file_attributes', 0) & 0x400,
                    'Reparse points are unsupported: ' + str(component))
    return path


def archive_group(relative):
    relative = safe_relative(relative)
    require(re.search(r'\d{5}\.pac$', relative, re.I), 'PAC filename lacks part suffix')
    return relative[:-9].lower()


@dataclass
class Entry:
    ordinal: int
    metadata: bytes
    packed: int
    size: int
    compression: int
    offset: int

    @property
    def name(self):
        return self.metadata[8:268].split(b'\0', 1)[0]


def inspect_archive(stream, filename):
    header = read_at(stream, 0, 20)
    require(header[:8] == b'DW_PACK\0', 'Not a DW_PACK archive')
    unknown, count, part = struct.unpack_from('<III', header, 8)
    require(unknown == 0 and 0 < count <= 65536 and part <= 65535, 'Invalid PAC header')
    archive_group(Path(filename).name)
    require(part == int(Path(filename).stem[-5:]), 'PAC filename/header part mismatch')
    table_size = 20 + count * 288
    table = read_at(stream, 20, count * 288)
    stream.seek(0, 2)
    total = stream.tell()
    entries = []
    for index in range(count):
        raw = table[index * 288:(index + 1) * 288]
        require(b'\0' in raw[8:268], 'Unterminated asset name')
        packed, size, compression, offset = struct.unpack_from('<IIII', raw, 272)
        require(compression in (0, 1), 'Unsupported compression')
        require(compression or packed == size, 'Raw size mismatch')
        require(table_size + offset <= total and packed <= total - table_size - offset, 'Payload outside PAC')
        require(size < 0x80000000, 'Asset exceeds native signed-seek range')
        entries.append(Entry(index, raw, packed, size, compression, table_size + offset))
    return part, table_size, entries


class Decoder:
    def __init__(self, library=None):
        path = library or Path(__file__).resolve().parents[1] / 'build/asset-build/asset-decoder.dll'
        self.library = ctypes.CDLL(str(path))
        self.fn = self.library.RebirthsDecodeHuffman
        self.fn.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32]
        self.fn.restype = ctypes.c_int

    def block(self, encoded, size):
        require(len(encoded) <= BLOCK_LIMIT and 0 <= size <= BLOCK_LIMIT, 'Block exceeds bound')
        output = ctypes.create_string_buffer(size)
        require(self.fn(encoded, len(encoded), output, size) == 1, 'Malformed Huffman block')
        return output.raw

    def decode(self, stream, entry):
        if not entry.compression:
            for start in range(0, entry.size, CHUNK):
                yield read_at(stream, entry.offset + start, min(CHUNK, entry.size - start))
            return
        require(entry.packed >= 16, 'Truncated divided-Huffman header')
        magic, count, block_size, header_size = struct.unpack('<IIII', read_at(stream, entry.offset, 16))
        require(magic == 0x1234 and 0 < block_size <= BLOCK_LIMIT and count <= 1048576,
                'Invalid divided-Huffman header')
        require(header_size == 16 + count * 12 and header_size <= entry.packed,
                'Invalid descriptor table size')
        require(count == (entry.size + block_size - 1) // block_size, 'Invalid block count')
        descriptors = read_at(stream, entry.offset + 16, count * 12)
        written = 0
        for i in range(count):
            size, packed, relative = struct.unpack_from('<III', descriptors, i * 12)
            require(size == min(block_size, entry.size - written) and 0 < packed <= BLOCK_LIMIT,
                    'Invalid descriptor sizes')
            require(header_size + relative + packed <= entry.packed, 'Block outside payload')
            try:
                decoded = self.block(read_at(stream, entry.offset + header_size + relative, packed), size)
            except ValueError as error:
                raise ValueError(f'{getattr(stream, "name", "<stream>")}: entry {entry.ordinal}, block {i}: {error}') from error
            yield decoded
            written += size
        require(written == entry.size, 'Decoded size mismatch')


def digest_range(stream, start, length):
    digest = hashlib.sha256()
    for offset in range(0, length, CHUNK):
        digest.update(read_at(stream, start + offset, min(CHUNK, length - offset)))
    return digest.digest()


def ticks(path):
    return path.stat().st_mtime_ns // 100 + 116444736000000000


def encode_manifest(game, backend, sources, files):
    out = bytearray(MAGIC + struct.pack('<IIII', game, backend, len(sources), len(files)))
    def string(value):
        value = safe_relative(value).encode('ascii')
        out.extend(struct.pack('<I', len(value)) + value)
    for s in sources:
        string(s['path']); string(s['group'])
        out.extend(struct.pack('<IQQQ', s['kind'], s['size'], s['ticks'], s['index_size']))
        out.extend(bytes.fromhex(s['hash']) + bytes.fromhex(s['index_hash']))
    for f in files:
        out.extend(struct.pack('<II', f['source'], f['id']))
        string(f['path'])
        out.extend(struct.pack('<QQI', f['offset'], f['storage_size'], f['size']))
        out.extend(bytes.fromhex(f['hash']) + bytes.fromhex(f['metadata']))
    require(len(out) <= 128 * 1024 * 1024, 'Manifest exceeds bound')
    return bytes(out) + hashlib.sha256(out).digest()


def decode_manifest(data):
    require(56 <= len(data) <= 128 * 1024 * 1024 + 32, 'Invalid manifest size')
    require(data[:8] == MAGIC and hashlib.sha256(data[:-32]).digest() == data[-32:], 'Manifest integrity failure')
    stream = io.BytesIO(data[:-32]); stream.seek(8)
    def take(n):
        value = stream.read(n); require(len(value) == n, 'Truncated manifest'); return value
    def unpack(fmt):
        return struct.unpack(fmt, take(struct.calcsize(fmt)))
    def string():
        n, = unpack('<I'); require(0 < n < 4096, 'Invalid path length')
        return safe_relative(take(n).decode('ascii'))
    game, backend, ns, nf = unpack('<IIII')
    require(game < 4 and backend < 2 and ns <= 100000 and nf <= 1000000, 'Invalid manifest header')
    sources = []
    for _ in range(ns):
        path, group = string(), string()
        kind, size, time, index_size = unpack('<IQQQ')
        require(kind in (0, 1) and index_size <= size, 'Invalid source record')
        sources.append(dict(path=path, group=group, kind=kind, size=size, ticks=time,
                            index_size=index_size, hash=take(32).hex(), index_hash=take(32).hex()))
    files = []; keys = set()
    for _ in range(nf):
        source, identifier = unpack('<II'); path = string(); offset, storage_size, size = unpack('<QQI')
        sha, metadata = take(32).hex(), take(288)
        require(source < ns and sources[source]['kind'] == 1, 'Invalid asset source')
        require(offset + size < 0x80000000 and (backend != 0 or offset == 0), 'Invalid asset range')
        require(offset + size <= storage_size < 0x80000000 and (backend != 0 or storage_size == size), 'Invalid backing size')
        require(struct.unpack_from('<I', metadata, 276)[0] == size and b'\0' in metadata[8:268], 'Asset metadata mismatch')
        key = (sources[source]['group'], identifier)
        require(key not in keys, 'Duplicate original entry identity'); keys.add(key)
        files.append(dict(source=source, id=identifier, path=path, offset=offset, storage_size=storage_size, size=size,
                          hash=sha, metadata=metadata.hex()))
    require(stream.tell() == len(data) - 32, 'Trailing manifest data')
    return game, backend, sources, files
