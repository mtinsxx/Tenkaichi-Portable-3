#!/usr/bin/env python3
"""Source-independent tests for the user-owned PSP texture converter."""
import importlib.util
import json
import pathlib
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE / "convert_psp_t8.py"
spec = importlib.util.spec_from_file_location("convert_psp_t8", SRC)
converter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(converter)


class NativeT8ConversionTests(unittest.TestCase):
    def test_header_matches_psp_t8p1_and_source_content(self):
        data = bytes((i * 13 + 7) & 255 for i in range(32 * 16))
        row = {"tbp": 336, "tbw": 8, "gen": "0xabcdef01",
               "width": 32, "height": 16}
        name, blob = converter.make_pack(row, data)
        self.assertEqual(name, "t8_0150_abcdef01_32x16.tp8")
        self.assertEqual(len(blob), 32 + len(data))
        self.assertEqual(converter.check_pack(blob), (name, len(data)))
        self.assertEqual(blob[32:], data)
        self.assertEqual(converter.HEADER.unpack_from(blob)[2:7],
                         (336, 8, 0xabcdef01, 32, 16))
        self.assertEqual(converter.make_pack(row, data)[1], blob)
        with self.assertRaises(ValueError):
            converter.check_pack(blob[:-1])
        with self.assertRaises(ValueError):
            converter.check_pack(blob[:32] + bytes([blob[32] ^ 1]) + blob[33:])
        with self.assertRaises(ValueError):
            converter.check_pack(b"BAD!" + blob[4:])

    def test_manifest_pack_verify_and_collision(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            image = bytes(range(128)) * 2
            (root / "tex.idx8").write_bytes(image)
            manifest = root / "index.json"
            row = {"source": "tex.idx8", "tbp": 368, "tbw": 4,
                   "gen": "0x13572468", "width": 16, "height": 16}
            manifest.write_text(json.dumps({"textures": [row]}))
            dest = root / "native"
            converter.convert(manifest, dest)
            self.assertTrue((dest / "enable.txt").is_file())
            converter.verify(dest)
            pack = dest / converter.pack_name(368, 0x13572468, 16, 16)
            self.assertEqual(converter.check_pack(pack.read_bytes())[1], 256)
            converter.convert(manifest, dest)
            self.assertEqual(converter.check_pack(pack.read_bytes())[1], 256)
            row2 = dict(row, source="../invalid.idx8")
            manifest.write_text(json.dumps({"textures": [row2]}))
            with self.assertRaises(ValueError):
                converter.convert(manifest, dest)

    def test_reject_invalid_dimensions_and_traversal(self):
        with self.assertRaises(ValueError):
            converter.make_pack({"tbp": 1, "tbw": 2, "gen": 3,
                                 "width": 0, "height": 8}, b"")
        with self.assertRaises(ValueError):
            converter.make_pack({"tbp": 16384, "tbw": 2, "gen": 3,
                                 "width": 2, "height": 2}, b"1234")
        with self.assertRaises(ValueError):
            converter.make_pack({"tbp": 1, "tbw": 64, "gen": 3,
                                 "width": 2, "height": 2}, b"1234")
        with self.assertRaises(ValueError):
            converter.make_pack({"tbp": 1, "tbw": 2, "gen": 3,
                                 "width": 2, "height": 2}, b"123")
        with self.assertRaises(ValueError):
            converter.check_pack(b"NOPE")


if __name__ == "__main__":
    unittest.main()
