#!/usr/bin/env python3
import argparse
import math
import struct
from pathlib import Path


BYTES_PER_SECTOR = 512
TOTAL_SECTORS = 32768
RESERVED_SECTORS = 32
FAT_COUNT = 2
SECTORS_PER_CLUSTER = 8
ROOT_CLUSTER = 2
MEDIA_DESCRIPTOR = 0xF8


class FileSpec:
    def __init__(self, source: Path, image_name: str) -> None:
        self.source = source
        self.image_name = image_name


def align_up(value: int, unit: int) -> int:
    return ((value + unit - 1) // unit) * unit


def short_name_checksum(name83: bytes) -> int:
    total = 0
    for value in name83:
        total = (((total & 1) << 7) + (total >> 1) + value) & 0xFF
    return total


def sanitize_short_char(ch: str) -> str:
    upper = ch.upper()
    if "A" <= upper <= "Z" or "0" <= upper <= "9":
        return upper
    if upper in "_$~!#%-{}()@^&":
        return upper
    return "_"


def make_short_alias(long_name: str, serial: int) -> bytes:
    stem, dot, suffix = long_name.rpartition(".")
    if not dot:
        stem = long_name
        suffix = ""

    digits = str(serial)
    prefix_len = max(1, 8 - 1 - len(digits))
    base = "".join(sanitize_short_char(ch) for ch in stem if ch != ".")[:prefix_len]
    if not base:
        base = "X"
    ext = "".join(sanitize_short_char(ch) for ch in suffix)[:3]
    return (base + "~" + digits).encode("ascii").ljust(8, b" ")[:8] + ext.encode("ascii").ljust(3, b" ")[:3]


def make_lfn_entries(long_name: str, checksum: int) -> list[bytes]:
    utf16 = [ord(ch) for ch in long_name]
    utf16.append(0x0000)
    while len(utf16) % 13 != 0:
        utf16.append(0xFFFF)

    chunks = [utf16[i:i + 13] for i in range(0, len(utf16), 13)]
    entries: list[bytes] = []
    total = len(chunks)
    for chunk_index, chunk in enumerate(reversed(chunks), start=1):
        order = total - chunk_index + 1
        if chunk_index == 1:
            order |= 0x40

        entry = bytearray(32)
        entry[0] = order
        for i, value in enumerate(chunk[0:5]):
            struct.pack_into("<H", entry, 1 + i * 2, value)
        entry[11] = 0x0F
        entry[12] = 0
        entry[13] = checksum
        for i, value in enumerate(chunk[5:11]):
            struct.pack_into("<H", entry, 14 + i * 2, value)
        struct.pack_into("<H", entry, 26, 0)
        for i, value in enumerate(chunk[11:13]):
            struct.pack_into("<H", entry, 28 + i * 2, value)
        entries.append(bytes(entry))
    return entries


def make_dir_entry(name83: bytes, attr: int, first_cluster: int, size: int) -> bytes:
    entry = bytearray(32)
    entry[0:11] = name83
    entry[11] = attr
    struct.pack_into("<H", entry, 20, (first_cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", entry, 26, first_cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return bytes(entry)


def set_fat32_entry(fat: bytearray, cluster: int, value: int) -> None:
    struct.pack_into("<I", fat, cluster * 4, value & 0x0FFFFFFF)


def compute_fat_sectors() -> int:
    sectors_per_fat = 1
    while True:
        data_sectors = TOTAL_SECTORS - RESERVED_SECTORS - FAT_COUNT * sectors_per_fat
        cluster_count = data_sectors // SECTORS_PER_CLUSTER
        needed = align_up((cluster_count + 2) * 4, BYTES_PER_SECTOR) // BYTES_PER_SECTOR
        if needed == sectors_per_fat:
            return sectors_per_fat
        sectors_per_fat = needed


def cluster_offset(data_start_sector: int, cluster: int) -> int:
    sector = data_start_sector + (cluster - 2) * SECTORS_PER_CLUSTER
    return sector * BYTES_PER_SECTOR


def allocate_clusters(next_cluster: int, fat: bytearray, size: int) -> tuple[int, int]:
    if size == 0:
        return 0, next_cluster

    clusters_needed = math.ceil(size / (BYTES_PER_SECTOR * SECTORS_PER_CLUSTER))
    first = next_cluster
    for i in range(clusters_needed):
        current = next_cluster + i
        value = 0x0FFFFFFF if i + 1 == clusters_needed else current + 1
        set_fat32_entry(fat, current, value)
    return first, next_cluster + clusters_needed


def write_file_data(image: bytearray, data_start_sector: int, first_cluster: int, data: bytes) -> None:
    if first_cluster == 0:
        return
    cluster_size = BYTES_PER_SECTOR * SECTORS_PER_CLUSTER
    offset = 0
    cluster = first_cluster
    while offset < len(data):
        chunk = data[offset:offset + cluster_size]
        start = cluster_offset(data_start_sector, cluster)
        image[start:start + len(chunk)] = chunk
        offset += len(chunk)
        cluster += 1


def parse_file_arg(value: str) -> FileSpec:
    source, sep, image_name = value.partition(":")
    if not sep:
        raise SystemExit(f"{value}: expected SOURCE:IMAGE_NAME")
    return FileSpec(Path(source), image_name.strip("/").lower())


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--kernel", action="append", default=[])
    parser.add_argument("--extra-file-auto", action="append", default=[])
    parser.add_argument("--file", action="append", default=[])
    return parser.parse_args()


def build_file_list(args: argparse.Namespace) -> list[FileSpec]:
    specs: list[FileSpec] = []
    for kernel in args.kernel:
        path = Path(kernel)
        specs.append(FileSpec(path, path.name.lower()))
    for value in args.extra_file_auto:
        path = Path(value)
        specs.append(FileSpec(path, path.name.lower()))
    for value in args.file:
        specs.append(parse_file_arg(value))
    specs.append(FileSpec(Path("/dev/null"), "hello.txt"))
    return specs


def main() -> int:
    args = parse_args()
    specs = build_file_list(args)
    sectors_per_fat = compute_fat_sectors()
    data_start_sector = RESERVED_SECTORS + FAT_COUNT * sectors_per_fat
    image = bytearray(TOTAL_SECTORS * BYTES_PER_SECTOR)

    boot = bytearray(BYTES_PER_SECTOR)
    boot[0:3] = b"\xEB\x58\x90"
    boot[3:11] = b"OS64F32 "
    struct.pack_into("<H", boot, 11, BYTES_PER_SECTOR)
    boot[13] = SECTORS_PER_CLUSTER
    struct.pack_into("<H", boot, 14, RESERVED_SECTORS)
    boot[16] = FAT_COUNT
    struct.pack_into("<H", boot, 17, 0)
    struct.pack_into("<H", boot, 19, 0)
    boot[21] = MEDIA_DESCRIPTOR
    struct.pack_into("<H", boot, 22, 0)
    struct.pack_into("<H", boot, 24, 32)
    struct.pack_into("<H", boot, 26, 64)
    struct.pack_into("<I", boot, 28, 0)
    struct.pack_into("<I", boot, 32, TOTAL_SECTORS)
    struct.pack_into("<I", boot, 36, sectors_per_fat)
    struct.pack_into("<H", boot, 40, 0)
    struct.pack_into("<H", boot, 42, 0)
    struct.pack_into("<I", boot, 44, ROOT_CLUSTER)
    struct.pack_into("<H", boot, 48, 1)
    struct.pack_into("<H", boot, 50, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, 0x20260614)
    boot[71:82] = b"OS64 ROOT  "
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xAA"
    image[0:BYTES_PER_SECTOR] = boot
    image[6 * BYTES_PER_SECTOR:7 * BYTES_PER_SECTOR] = boot

    fsinfo = bytearray(BYTES_PER_SECTOR)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<I", fsinfo, 488, 0xFFFFFFFF)
    struct.pack_into("<I", fsinfo, 492, 0xFFFFFFFF)
    fsinfo[510:512] = b"\x55\xAA"
    image[BYTES_PER_SECTOR:2 * BYTES_PER_SECTOR] = fsinfo

    fat = bytearray(sectors_per_fat * BYTES_PER_SECTOR)
    set_fat32_entry(fat, 0, 0x0FFFFFF8)
    set_fat32_entry(fat, 1, 0x0FFFFFFF)
    set_fat32_entry(fat, ROOT_CLUSTER, 0x0FFFFFFF)

    root_entries: list[bytes] = []
    next_cluster = ROOT_CLUSTER + 1
    used_aliases: set[bytes] = set()

    for index, spec in enumerate(specs, start=1):
        if spec.image_name == "hello.txt":
            data = b"hello fat32 root\n"
        else:
            if not spec.source.exists():
                raise SystemExit(f"{spec.source}: missing input file")
            data = spec.source.read_bytes()

        alias_serial = index
        alias = make_short_alias(spec.image_name, alias_serial)
        while alias in used_aliases:
            alias_serial += 1
            alias = make_short_alias(spec.image_name, alias_serial)
        used_aliases.add(alias)

        first_cluster, next_cluster = allocate_clusters(next_cluster, fat, len(data))
        checksum = short_name_checksum(alias)
        root_entries.extend(make_lfn_entries(spec.image_name, checksum))
        root_entries.append(make_dir_entry(alias, 0x20, first_cluster, len(data)))
        write_file_data(image, data_start_sector, first_cluster, data)

    root_capacity = BYTES_PER_SECTOR * SECTORS_PER_CLUSTER
    root_bytes = b"".join(root_entries)
    if len(root_bytes) + 32 > root_capacity:
        raise SystemExit("root directory does not fit in one FAT32 cluster")
    root_start = cluster_offset(data_start_sector, ROOT_CLUSTER)
    image[root_start:root_start + len(root_bytes)] = root_bytes

    fat1_offset = RESERVED_SECTORS * BYTES_PER_SECTOR
    fat2_offset = (RESERVED_SECTORS + sectors_per_fat) * BYTES_PER_SECTOR
    image[fat1_offset:fat1_offset + len(fat)] = fat
    image[fat2_offset:fat2_offset + len(fat)] = fat

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
