#!/usr/bin/env python3
import argparse
import struct


BYTES_PER_SECTOR = 512
TOTAL_SECTORS = 256
RESERVED_SECTORS = 32
FAT_COUNT = 2
SECTORS_PER_FAT = 1
SECTORS_PER_CLUSTER = 1
ROOT_CLUSTER = 2
MEDIA_DESCRIPTOR = 0xF8


def name83_bytes(filename: str) -> bytes:
    parts = filename.split(".")
    if len(parts) != 2:
        raise SystemExit(f"{filename}: file name must be in 8.3 format")

    name, ext = parts
    if not name or not ext or len(name) > 8 or len(ext) > 3:
        raise SystemExit(f"{filename}: file name must be in 8.3 format")

    return name.upper().encode("ascii").ljust(8, b" ") + ext.upper().encode("ascii").ljust(3, b" ")


def dir83_bytes(name: str) -> bytes:
    if not name or "." in name or len(name) > 8:
        raise SystemExit(f"{name}: directory name must fit 8 characters without an extension")
    return name.upper().encode("ascii").ljust(8, b" ") + b"   "


def set_fat32_entry(fat: bytearray, cluster: int, value: int) -> None:
    struct.pack_into("<I", fat, cluster * 4, value & 0x0FFFFFFF)


def make_dir_entry(name83: bytes, attr: int, first_cluster: int, size: int) -> bytes:
    entry = bytearray(32)
    entry[0:11] = name83
    entry[11] = attr
    struct.pack_into("<H", entry, 20, (first_cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", entry, 26, first_cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return bytes(entry)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    image = bytearray(TOTAL_SECTORS * BYTES_PER_SECTOR)

    boot = bytearray(BYTES_PER_SECTOR)
    boot[0:3] = b"\xEB\x58\x90"
    boot[3:11] = b"MYOSF32 "
    struct.pack_into("<H", boot, 11, BYTES_PER_SECTOR)
    boot[13] = SECTORS_PER_CLUSTER
    struct.pack_into("<H", boot, 14, RESERVED_SECTORS)
    boot[16] = FAT_COUNT
    struct.pack_into("<H", boot, 17, 0)  # root_entry_count
    struct.pack_into("<H", boot, 19, 0)  # total_sectors_16
    boot[21] = MEDIA_DESCRIPTOR
    struct.pack_into("<H", boot, 22, 0)  # sectors_per_fat_16
    struct.pack_into("<H", boot, 24, 32)  # sectors_per_track
    struct.pack_into("<H", boot, 26, 64)  # heads
    struct.pack_into("<I", boot, 28, 0)  # hidden
    struct.pack_into("<I", boot, 32, TOTAL_SECTORS)
    struct.pack_into("<I", boot, 36, SECTORS_PER_FAT)
    struct.pack_into("<H", boot, 40, 0)
    struct.pack_into("<H", boot, 42, 0)
    struct.pack_into("<I", boot, 44, ROOT_CLUSTER)
    struct.pack_into("<H", boot, 48, 1)  # fsinfo
    struct.pack_into("<H", boot, 50, 6)  # backup boot sector
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, 0x12345678)
    boot[71:82] = b"MYOS FAT32 "
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xAA"
    image[0:BYTES_PER_SECTOR] = boot

    fsinfo = bytearray(BYTES_PER_SECTOR)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<I", fsinfo, 488, 0xFFFFFFFF)
    struct.pack_into("<I", fsinfo, 492, 0xFFFFFFFF)
    fsinfo[510:512] = b"\x55\xAA"
    image[BYTES_PER_SECTOR:2 * BYTES_PER_SECTOR] = fsinfo
    image[6 * BYTES_PER_SECTOR:7 * BYTES_PER_SECTOR] = boot

    fat = bytearray(SECTORS_PER_FAT * BYTES_PER_SECTOR)
    set_fat32_entry(fat, 0, 0x0FFFFFF8)
    set_fat32_entry(fat, 1, 0x0FFFFFFF)
    set_fat32_entry(fat, 2, 0x0FFFFFFF)  # root dir
    set_fat32_entry(fat, 3, 0x0FFFFFFF)  # HELLO.TXT
    set_fat32_entry(fat, 4, 0x0FFFFFFF)  # DOCS dir
    set_fat32_entry(fat, 5, 0x0FFFFFFF)  # README.TXT

    fat1_offset = RESERVED_SECTORS * BYTES_PER_SECTOR
    fat2_offset = (RESERVED_SECTORS + SECTORS_PER_FAT) * BYTES_PER_SECTOR
    image[fat1_offset:fat1_offset + len(fat)] = fat
    image[fat2_offset:fat2_offset + len(fat)] = fat

    data_start_sector = RESERVED_SECTORS + FAT_COUNT * SECTORS_PER_FAT

    def cluster_offset(cluster: int) -> int:
        sector = data_start_sector + (cluster - 2) * SECTORS_PER_CLUSTER
        return sector * BYTES_PER_SECTOR

    root_dir = bytearray(BYTES_PER_SECTOR)
    root_dir[0:32] = make_dir_entry(name83_bytes("HELLO.TXT"), 0x20, 3, len(b"hello fat32\n"))
    root_dir[32:64] = make_dir_entry(dir83_bytes("DOCS"), 0x10, 4, 0)
    image[cluster_offset(2):cluster_offset(2) + BYTES_PER_SECTOR] = root_dir

    hello_cluster = bytearray(BYTES_PER_SECTOR)
    hello_cluster[0:len(b"hello fat32\n")] = b"hello fat32\n"
    image[cluster_offset(3):cluster_offset(3) + BYTES_PER_SECTOR] = hello_cluster

    docs_dir = bytearray(BYTES_PER_SECTOR)
    docs_dir[0:32] = make_dir_entry(name83_bytes("README.TXT"), 0x20, 5, len(b"fat32 nested\n"))
    image[cluster_offset(4):cluster_offset(4) + BYTES_PER_SECTOR] = docs_dir

    readme_cluster = bytearray(BYTES_PER_SECTOR)
    readme_cluster[0:len(b"fat32 nested\n")] = b"fat32 nested\n"
    image[cluster_offset(5):cluster_offset(5) + BYTES_PER_SECTOR] = readme_cluster

    with open(args.output, "wb") as f:
        f.write(image)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
