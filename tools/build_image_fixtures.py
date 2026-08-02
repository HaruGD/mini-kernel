#!/usr/bin/env python3
import argparse
import binascii
import struct
import zlib
from pathlib import Path

from png_to_osimg import convert


def chunk(kind: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + kind + data +
            struct.pack(">I", binascii.crc32(kind + data) & 0xFFFFFFFF))


def make_png(path: Path) -> None:
    width = height = 16
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            alpha = 64 + (x * 191 // (width - 1))
            rows.extend((40 + x * 10, 90 + y * 7, 230 - y * 8, alpha))
    data = (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height,
                                         8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(rows), 9)) +
            chunk(b"IEND", b""))
    path.write_bytes(data)


def make_bmp(path: Path) -> None:
    width = height = 8
    row_bytes = width * 4
    pixels = bytearray()
    for y in range(height - 1, -1, -1):
        for x in range(width):
            pixels.extend((30 + y * 20, 180 - x * 12, 60 + x * 20, 255))
    offset = 54
    header = (b"BM" + struct.pack("<IHHI", offset + len(pixels), 0, 0, offset) +
              struct.pack("<IIIHHIIIIII", 40, width, height, 1, 32, 0,
                          len(pixels), 0, 0, 0, 0))
    path.write_bytes(header + pixels)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    png = args.output_dir / "image_demo.png"
    bmp = args.output_dir / "image_demo.bmp"
    osimg = args.output_dir / "image_demo.osimg"
    make_png(png)
    make_bmp(bmp)
    convert(png, osimg)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
