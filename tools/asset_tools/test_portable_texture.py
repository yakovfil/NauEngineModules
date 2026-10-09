import pathlib
import struct
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image
from portable_texture import normalize_texture, HEADER


class PortableTexture(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = pathlib.Path(self.directory.name) / "marker.png"

    def test_rgb_and_rgba_orientation(self):
        for mode in ("RGB", "RGBA"):
            pixels = [(10, 20, 30, 40), (80, 90, 100, 110)]
            image = Image.new(mode, (1, 2))
            image.putdata([pixel[:3] if mode == "RGB" else pixel for pixel in pixels])
            image.save(self.path)
            result = normalize_texture(self.path)
            self.assertEqual(HEADER.unpack(result[:24]), (b"NTEX", 1, 1, 2, 1, 8))
            self.assertEqual(result[24:], bytes(value for pixel in pixels for value in (pixel if mode == "RGBA" else (*pixel[:3], 255))))
            self.assertEqual(result, normalize_texture(self.path))

    def test_unsupported_modes(self):
        for mode in ("L", "P", "I;16"):
            Image.new(mode, (2, 2)).save(self.path)
            with self.assertRaisesRegex(RuntimeError, "8-bit RGB/RGBA"):
                normalize_texture(self.path)

    def test_animation(self):
        image = Image.new("RGBA", (2, 2), "red")
        image.save(self.path, save_all=True, append_images=[Image.new("RGBA", (2, 2), "blue")])
        with self.assertRaisesRegex(RuntimeError, "single"):
            normalize_texture(self.path)

    def test_corruption_missing_and_limits(self):
        with self.assertRaisesRegex(RuntimeError, "marker.png"):
            normalize_texture(self.path)
        self.path.write_bytes(b"not a PNG")
        with self.assertRaises(RuntimeError):
            normalize_texture(self.path)
        Image.new("RGBA", (2, 2)).save(self.path)
        source = self.path.read_bytes()
        for width in (0, 4097, 0xffffffff):
            data = bytearray(source)
            data[16:20] = struct.pack(">I", width)
            self.path.write_bytes(data)
            with self.assertRaisesRegex(RuntimeError, "dimensions"):
                normalize_texture(self.path)

    def test_missing_decoder(self):
        import builtins
        original = builtins.__import__
        def missing(name, *args, **kwargs):
            if name == "PIL": raise ImportError("missing")
            return original(name, *args, **kwargs)
        with patch("builtins.__import__", missing):
            with self.assertRaisesRegex(RuntimeError, "pip install Pillow"):
                normalize_texture(self.path)


if __name__ == "__main__":
    unittest.main()
