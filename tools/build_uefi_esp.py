#!/usr/bin/env python3
import argparse
import math
import struct


BYTES_PER_SECTOR = 512
TOTAL_SECTORS = 32768
SECTORS_PER_CLUSTER = 1
RESERVED_SECTORS = 1
FAT_COUNT = 2
ROOT_ENTRY_COUNT = 512
MEDIA_DESCRIPTOR = 0xF8


def name83(name: str) -> bytes:
    if name == ".":
        return b".          "
    if name == "..":
        return b"..         "
    if "." in name:
        stem, ext = name.split(".", 1)
    else:
        stem, ext = name, ""
    if not stem or len(stem) > 8 or len(ext) > 3:
        raise SystemExit(f"{name}: not an 8.3 name")
    return stem.upper().encode("ascii").ljust(8, b" ") + ext.upper().encode("ascii").ljust(3, b" ")


def cluster_count(size: int) -> int:
    return max(1, math.ceil(size / (BYTES_PER_SECTOR * SECTORS_PER_CLUSTER)))


def make_entry(name: str, attr: int, first_cluster: int, size: int) -> bytes:
    entry = bytearray(32)
    entry[0:11] = name83(name)
    entry[11] = attr
    struct.pack_into("<H", entry, 26, first_cluster)
    struct.pack_into("<I", entry, 28, size)
    return bytes(entry)


def set_fat16_entry(fat: bytearray, cluster: int, value: int) -> None:
    struct.pack_into("<H", fat, cluster * 2, value & 0xFFFF)


def write_dir(image: bytearray, offset: int, entries: list[bytes]) -> None:
    for index, entry in enumerate(entries):
        image[offset + index * 32:offset + (index + 1) * 32] = entry


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--efi", required=True)
    parser.add_argument("--kernel", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    with open(args.efi, "rb") as f:
        efi = f.read()
    with open(args.kernel, "rb") as f:
        kernel = f.read()
    startup = b"FS0:\r\nBOOTX64.EFI\r\n"

    root_dir_sectors = (ROOT_ENTRY_COUNT * 32 + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
    data_sectors_guess = TOTAL_SECTORS - RESERVED_SECTORS - root_dir_sectors
    sectors_per_fat = math.ceil((data_sectors_guess // SECTORS_PER_CLUSTER + 2) * 2 / BYTES_PER_SECTOR)
    data_start_sector = RESERVED_SECTORS + FAT_COUNT * sectors_per_fat + root_dir_sectors
    data_sectors = TOTAL_SECTORS - data_start_sector
    total_clusters = data_sectors // SECTORS_PER_CLUSTER

    if total_clusters < 4085:
        raise SystemExit("image is too small for FAT16")

    image = bytearray(TOTAL_SECTORS * BYTES_PER_SECTOR)

    boot = bytearray(BYTES_PER_SECTOR)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"MYOSUEFI"
    struct.pack_into("<H", boot, 11, BYTES_PER_SECTOR)
    boot[13] = SECTORS_PER_CLUSTER
    struct.pack_into("<H", boot, 14, RESERVED_SECTORS)
    boot[16] = FAT_COUNT
    struct.pack_into("<H", boot, 17, ROOT_ENTRY_COUNT)
    struct.pack_into("<H", boot, 19, TOTAL_SECTORS if TOTAL_SECTORS < 65536 else 0)
    boot[21] = MEDIA_DESCRIPTOR
    struct.pack_into("<H", boot, 22, sectors_per_fat)
    struct.pack_into("<H", boot, 24, 32)
    struct.pack_into("<H", boot, 26, 64)
    struct.pack_into("<I", boot, 28, 0)
    struct.pack_into("<I", boot, 32, TOTAL_SECTORS if TOTAL_SECTORS >= 65536 else 0)
    boot[36] = 0x80
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0x55454649)
    boot[43:54] = b"MYOS UEFI  "
    boot[54:62] = b"FAT16   "
    boot[510:512] = b"\x55\xAA"
    image[0:BYTES_PER_SECTOR] = boot

    fat = bytearray(sectors_per_fat * BYTES_PER_SECTOR)
    set_fat16_entry(fat, 0, 0xFFF8)
    set_fat16_entry(fat, 1, 0xFFFF)

    next_cluster = 2

    def alloc_chain(data: bytes) -> list[int]:
        nonlocal next_cluster
        count = cluster_count(len(data))
        chain = list(range(next_cluster, next_cluster + count))
        next_cluster += count
        for i, cluster in enumerate(chain):
            set_fat16_entry(fat, cluster, 0xFFFF if i + 1 == len(chain) else chain[i + 1])
        return chain

    efi_dir_chain = alloc_chain(b"")
    boot_dir_chain = alloc_chain(b"")
    efi_chain = alloc_chain(efi)
    kernel_chain = alloc_chain(kernel)
    startup_chain = alloc_chain(startup)

    if next_cluster >= total_clusters:
        raise SystemExit("files do not fit in ESP image")

    for fat_index in range(FAT_COUNT):
        fat_offset = (RESERVED_SECTORS + fat_index * sectors_per_fat) * BYTES_PER_SECTOR
        image[fat_offset:fat_offset + len(fat)] = fat

    def cluster_offset(cluster: int) -> int:
        sector = data_start_sector + (cluster - 2) * SECTORS_PER_CLUSTER
        return sector * BYTES_PER_SECTOR

    root_offset = (RESERVED_SECTORS + FAT_COUNT * sectors_per_fat) * BYTES_PER_SECTOR
    write_dir(image, root_offset, [
        make_entry("EFI", 0x10, efi_dir_chain[0], 0),
        make_entry("BOOTX64.EFI", 0x20, efi_chain[0], len(efi)),
        make_entry("KERNEL.BIN", 0x20, kernel_chain[0], len(kernel)),
        make_entry("STARTUP.NSH", 0x20, startup_chain[0], len(startup)),
    ])

    efi_dir = [
        make_entry(".", 0x10, efi_dir_chain[0], 0),
        make_entry("..", 0x10, 0, 0),
        make_entry("BOOT", 0x10, boot_dir_chain[0], 0),
    ]
    write_dir(image, cluster_offset(efi_dir_chain[0]), efi_dir)

    boot_dir = [
        make_entry(".", 0x10, boot_dir_chain[0], 0),
        make_entry("..", 0x10, efi_dir_chain[0], 0),
        make_entry("BOOTX64.EFI", 0x20, efi_chain[0], len(efi)),
    ]
    write_dir(image, cluster_offset(boot_dir_chain[0]), boot_dir)

    def write_file(data: bytes, chain: list[int]) -> None:
        cluster_size = BYTES_PER_SECTOR * SECTORS_PER_CLUSTER
        for i, cluster in enumerate(chain):
            chunk = data[i * cluster_size:(i + 1) * cluster_size]
            offset = cluster_offset(cluster)
            image[offset:offset + len(chunk)] = chunk

    write_file(efi, efi_chain)
    write_file(kernel, kernel_chain)
    write_file(startup, startup_chain)

    with open(args.output, "wb") as f:
        f.write(image)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
