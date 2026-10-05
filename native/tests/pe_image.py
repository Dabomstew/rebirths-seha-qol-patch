"""Bounded, read-only PE32 mapping for immutable-baseline fixtures."""
import struct

class PeImage:
    def __init__(self, data):
        self.data = data
        assert data[:2] == b'MZ'
        nt = struct.unpack_from('<I', data, 0x3c)[0]
        assert data[nt:nt+4] == b'PE\0\0'
        assert struct.unpack_from('<H', data, nt+4)[0] == 0x14c
        self.optional = nt + 24
        assert struct.unpack_from('<H', data, self.optional)[0] == 0x10b
        self.image_base = struct.unpack_from('<I', data, self.optional+28)[0]
        count = struct.unpack_from('<H', data, nt+6)[0]
        table = self.optional + struct.unpack_from('<H', data, nt+20)[0]
        assert table + count*40 <= len(data)
        self.sections = []
        for index in range(count):
            offset = table + index*40
            virtual_size, address, raw_size, raw = struct.unpack_from('<IIII', data, offset+8)
            assert raw + raw_size <= len(data)
            flags = struct.unpack_from('<I', data, offset+36)[0]
            self.sections.append((address, min(virtual_size, raw_size), raw, flags))

    def mapped(self, rva, size, executable=False):
        assert rva >= 0 and size > 0
        for address, span, raw, flags in self.sections:
            if address <= rva and rva+size <= address+span:
                assert not executable or flags & 0x20000000, 'RVA is not executable'
                return self.data[raw+rva-address:raw+rva-address+size]
        raise AssertionError(f'unmapped file-backed RVA {rva:#x}+{size:#x}')

    def call_target(self, rva):
        instruction = self.mapped(rva, 5, executable=True)
        assert instruction[0] == 0xe8, 'expected direct CALL'
        return rva + 5 + struct.unpack_from('<i', instruction, 1)[0]

    def string(self, rva, limit=256):
        result = bytearray()
        for offset in range(limit):
            byte = self.mapped(rva+offset, 1)
            if byte == b'\0': return bytes(result)
            result.extend(byte)
        raise AssertionError('unterminated bounded PE string')
