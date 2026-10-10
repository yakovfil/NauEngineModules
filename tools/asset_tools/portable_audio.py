"""Validate the bounded portable PCM16 WAV subset before publishing assets."""
import struct


def validate_wav(raw):
    def invalid():
        raise ValueError("Unsupported or malformed click WAV: PCM16 mono/stereo 44.1/48 kHz, <=1 second, <=256 KiB required")
    if not 44 <= len(raw) <= 256 * 1024 or raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        invalid()
    if struct.unpack_from("<I", raw, 4)[0] != len(raw) - 8:
        invalid()
    at, fmt, payload = 12, None, None
    while at < len(raw):
        if len(raw) - at < 8:
            invalid()
        kind, size = struct.unpack_from("<4sI", raw, at)
        end = at + 8 + size
        if end + (size & 1) > len(raw):
            invalid()
        if kind == b"fmt ":
            if fmt is not None or size != 16:
                invalid()
            fmt = struct.unpack_from("<HHIIHH", raw, at + 8)
        elif kind == b"data":
            if payload is not None:
                invalid()
            payload = size
        at = end + (size & 1)
    if fmt is None or not payload:
        invalid()
    encoding, channels, rate, byte_rate, align, bits = fmt
    if encoding != 1 or channels not in (1, 2) or rate not in (44100, 48000) or bits != 16 or align != channels * 2 or byte_rate != rate * align or payload % align or payload // align > rate:
        invalid()
    return fmt
