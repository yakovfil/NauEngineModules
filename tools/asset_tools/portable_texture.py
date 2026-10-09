"""Normalize the limited portable billboard PNG subset to checked RGBA8."""
import struct
import sys

MAX_DIMENSION = 4096
HEADER = struct.Struct("<4sIIIII")


def require_decoder():
    try:
        from PIL import Image
    except ImportError as error:
        raise RuntimeError(f"Billboard preparation requires Pillow: {sys.executable} -m pip install Pillow") from error
    return Image


def normalize_texture(path):
    Image = require_decoder()
    try:
        with path.open("rb") as source:
            prefix = source.read(33)
        if len(prefix) != 33 or prefix[:8] != b"\x89PNG\r\n\x1a\n" or prefix[12:16] != b"IHDR" or prefix[24] != 8 or prefix[25] not in (2, 6):
            raise ValueError("requires an 8-bit RGB/RGBA PNG")
        width, height = struct.unpack(">II", prefix[16:24])
        if not (0 < width <= MAX_DIMENSION and 0 < height <= MAX_DIMENSION):
            raise ValueError("invalid or oversized dimensions")
        with Image.open(path) as image:
            if image.format != "PNG" or image.mode not in ("RGB", "RGBA") or getattr(image, "n_frames", 1) != 1:
                raise ValueError("requires a single RGB/RGBA PNG image")
            image.load()
            pixels = image.convert("RGBA").tobytes()
        if len(pixels) != width * height * 4:
            raise ValueError("inconsistent decoded pixel length")
        return HEADER.pack(b"NTEX", 1, width, height, 1, len(pixels)) + pixels
    except Exception as error:
        raise RuntimeError(f"Portable texture {path}: {error}") from error


if __name__ == "__main__":
    try:
        require_decoder()
    except RuntimeError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
