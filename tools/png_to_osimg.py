#!/usr/bin/env python3
import argparse
import binascii
import struct
import zlib
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
OSIMG_MAGIC = 0x4D49534F
OSIMG_VERSION = 1
OSIMG_FORMAT_BGRA_PREMULTIPLIED = 1
MAX_DIMENSION = 4096
MAX_PIXELS = 16 * 1024 * 1024


def paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    distances = (abs(estimate - left), abs(estimate - above),
                 abs(estimate - upper_left))
    return (left, above, upper_left)[distances.index(min(distances))]


def decode_png(data: bytes) -> tuple[int, int, bytes]:
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("invalid PNG signature")
    offset = len(PNG_SIGNATURE)
    width = height = channels = 0
    compressed = bytearray()
    saw_end = False
    chunks = 0
    while offset + 12 <= len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        if length > len(data) - offset - 12:
            raise ValueError("truncated PNG chunk")
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        expected_crc = struct.unpack_from(">I", data, offset + 8 + length)[0]
        if binascii.crc32(kind + payload) & 0xFFFFFFFF != expected_crc:
            raise ValueError("PNG CRC mismatch")
        chunks += 1
        if chunks > 1024:
            raise ValueError("too many PNG chunks")
        if kind == b"IHDR":
            if len(payload) != 13 or width:
                raise ValueError("invalid IHDR")
            width, height, depth, color, compression, filtering, interlace = \
                struct.unpack(">IIBBBBB", payload)
            channels = 3 if color == 2 else 4 if color == 6 else 0
            if (not 0 < width <= MAX_DIMENSION or
                    not 0 < height <= MAX_DIMENSION or
                    width * height > MAX_PIXELS or depth != 8 or
                    not channels or compression or filtering or interlace):
                raise ValueError("unsupported PNG layout")
        elif kind == b"IDAT":
            if not width or saw_end:
                raise ValueError("IDAT ordering")
            compressed.extend(payload)
            if len(compressed) > 32 * 1024 * 1024:
                raise ValueError("compressed PNG budget exceeded")
        elif kind == b"IEND":
            if payload or not compressed:
                raise ValueError("invalid IEND")
            saw_end = True
            offset += length + 12
            break
        elif kind[0] & 0x20 == 0:
            raise ValueError(f"unsupported critical chunk {kind!r}")
        offset += length + 12
    if not saw_end or offset != len(data):
        raise ValueError("missing or trailing IEND")
    row_bytes = width * channels
    expected = (row_bytes + 1) * height
    inflater = zlib.decompressobj()
    raw = inflater.decompress(bytes(compressed), expected + 1)
    raw += inflater.flush()
    if len(raw) != expected or not inflater.eof or inflater.unused_data:
        raise ValueError("PNG decompressed length mismatch")
    scanlines = bytearray(row_bytes * height)
    for y in range(height):
        filter_type = raw[y * (row_bytes + 1)]
        if filter_type > 4:
            raise ValueError("invalid PNG filter")
        source = raw[y * (row_bytes + 1) + 1:(y + 1) * (row_bytes + 1)]
        row_start = y * row_bytes
        for x, value in enumerate(source):
            left = scanlines[row_start + x - channels] if x >= channels else 0
            above = scanlines[row_start + x - row_bytes] if y else 0
            upper_left = (scanlines[row_start + x - row_bytes - channels]
                          if y and x >= channels else 0)
            predictor = (0 if filter_type == 0 else left if filter_type == 1
                         else above if filter_type == 2
                         else (left + above) // 2 if filter_type == 3
                         else paeth(left, above, upper_left))
            scanlines[row_start + x] = (value + predictor) & 0xFF
    pixels = bytearray(width * height * 4)
    for index in range(width * height):
        base = index * channels
        red, green, blue = scanlines[base:base + 3]
        alpha = scanlines[base + 3] if channels == 4 else 255
        red = (red * alpha + 127) // 255
        green = (green * alpha + 127) // 255
        blue = (blue * alpha + 127) // 255
        struct.pack_into("<I", pixels, index * 4,
                         alpha << 24 | red << 16 | green << 8 | blue)
    return width, height, bytes(pixels)


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def convert(input_path: Path, output_path: Path) -> None:
    width, height, pixels = decode_png(input_path.read_bytes())
    header = struct.pack("<IHHIIIIIII", OSIMG_MAGIC, OSIMG_VERSION, 36,
                         width, height, width,
                         OSIMG_FORMAT_BGRA_PREMULTIPLIED, len(pixels),
                         fnv1a(pixels), 0)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(header + pixels)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert bounded RGB/RGBA PNG assets to OS64 .osimg")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    convert(args.input, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
