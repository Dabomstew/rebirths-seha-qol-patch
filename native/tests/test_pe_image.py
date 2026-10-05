"""Independent malformed-image, mapping and CALL oracles for shared PE reads."""
import struct
import unittest
from pe_image import PeImage

class MappingTests(unittest.TestCase):
    def image(self):
        data = bytearray(0x400)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 0x80)
        data[0x80:0x84] = b'PE\0\0'
        struct.pack_into('<HH', data, 0x84, 0x14c, 1)
        struct.pack_into('<H', data, 0x94, 0xe0)
        struct.pack_into('<H', data, 0x98, 0x10b)
        struct.pack_into('<I', data, 0xb4, 0x400000)
        struct.pack_into('<IIII', data, 0x180, 0x300, 0x1000, 0x200, 0x200)
        struct.pack_into('<I', data, 0x19c, 0x20000000)
        data[0x200:0x205] = bytes.fromhex('e8fbffffff')  # target is its own RVA
        data[0x210:0x214] = b'abc\0'
        return data

    def test_mapping_and_backward_call(self):
        pe = PeImage(self.image())
        self.assertEqual(pe.image_base, 0x400000)
        self.assertEqual(pe.mapped(0x1000, 5, executable=True), bytes.fromhex('e8fbffffff'))
        self.assertEqual(pe.call_target(0x1000), 0x1000)
        self.assertEqual(pe.string(0x1010), b'abc')
        self.assertEqual(len(pe.mapped(0x11ff, 1)), 1)

    def test_zero_fill_gap_and_crossing_refused(self):
        pe = PeImage(self.image())
        for rva, size in ((0xfff, 1), (0x11ff, 2), (0x1200, 1), (0x1000, 0), (-1, 1)):
            with self.subTest(rva=rva, size=size), self.assertRaises(AssertionError): pe.mapped(rva, size)

    def test_nonexecutable_and_noncall_refused(self):
        data = self.image(); struct.pack_into('<I', data, 0x19c, 0x40000000)
        with self.assertRaises(AssertionError): PeImage(data).call_target(0x1000)
        data = self.image(); data[0x200] = 0x90
        with self.assertRaises(AssertionError): PeImage(data).call_target(0x1000)

    def test_unterminated_string_bounded(self):
        with self.assertRaises(AssertionError): PeImage(self.image()).string(0x1010, limit=3)

    def test_truncated_headers_sections_and_raw_data(self):
        for data in (self.image()[:0x40], self.image()[:0x190], self.image()[:0x300]):
            with self.subTest(size=len(data)), self.assertRaises((AssertionError, struct.error)): PeImage(data)

if __name__ == '__main__': unittest.main()
