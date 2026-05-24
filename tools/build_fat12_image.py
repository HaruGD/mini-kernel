#!/usr/bin/env python3
import argparse
import math
import struct


BYTES_PER_SECTOR = 512
TOTAL_SECTORS = 2880
SECTORS_PER_CLUSTER = 1
FAT_COUNT = 2
ROOT_ENTRY_COUNT = 224
SECTORS_PER_FAT = 9
MEDIA_DESCRIPTOR = 0xF0
KERNEL_LOAD_ADDR = 0x100000
STAGE2_LOAD_ADDR = 0x90000
KERNEL_CLUSTER_STRIDE = 2
HMA_LOAD_LIMIT = 0x10FFF0


def name83_bytes(filename):
    parts = filename.split(".")
    if len(parts) != 2:
        raise SystemExit(f"{filename}: extra file name must be in 8.3 format")

    name, ext = parts
    if not name or not ext or len(name) > 8 or len(ext) > 3:
        raise SystemExit(f"{filename}: extra file name must be in 8.3 format")

    try:
        name_bytes = name.upper().encode("ascii")
        ext_bytes = ext.upper().encode("ascii")
    except UnicodeEncodeError as exc:
        raise SystemExit(f"{filename}: extra file name must be ASCII") from exc

    return name_bytes.ljust(8, b" ") + ext_bytes.ljust(3, b" ")


def parse_extra_file(spec):
    if "=" not in spec:
        raise SystemExit(f"{spec}: extra file must look like NAME.EXT=/path/to/file")

    name, path = spec.split("=", 1)
    if not path:
        raise SystemExit(f"{spec}: missing path for extra file")

    return name83_bytes(name), path


def set_fat12_entry(fat, cluster, value):
    offset = cluster + (cluster // 2)
    value &= 0x0FFF
    if cluster & 1:
        fat[offset] = (fat[offset] & 0x0F) | ((value << 4) & 0xF0)
        fat[offset + 1] = (value >> 4) & 0xFF
    else:
        fat[offset] = value & 0xFF
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--boot", required=True)
    parser.add_argument("--stage2", required=True)
    parser.add_argument("--kernel", required=True)
    parser.add_argument("--extra-file", action="append", default=[])
    parser.add_argument("--output", required=True)
    parser.add_argument("--stage2-sectors", required=True, type=int)
    args = parser.parse_args()

    with open(args.boot, "rb") as f:
        boot = f.read()
    with open(args.stage2, "rb") as f:
        stage2 = f.read()
    with open(args.kernel, "rb") as f:
        kernel = f.read()
    extra_files = []
    for spec in args.extra_file:
        name83, path = parse_extra_file(spec)
        with open(path, "rb") as f:
            extra_files.append((name83, f.read()))

    if len(boot) != BYTES_PER_SECTOR:
        raise SystemExit("boot sector must be exactly 512 bytes")

    stage2_size = args.stage2_sectors * BYTES_PER_SECTOR
    if len(stage2) != stage2_size:
        raise SystemExit(f"stage2 must be padded to {stage2_size} bytes")

    if KERNEL_LOAD_ADDR + len(kernel) > HMA_LOAD_LIMIT:
        raise SystemExit("kernel is too large for the current HMA real-mode loader")

    reserved_sectors = 1 + args.stage2_sectors
    root_dir_sectors = (ROOT_ENTRY_COUNT * 32 + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
    root_start = reserved_sectors + FAT_COUNT * SECTORS_PER_FAT
    data_start = root_start + root_dir_sectors
    files_to_place = [(b"KERNEL  BIN", kernel)]
    files_to_place.extend(extra_files)

    placed_files = []
    next_cluster = 2
    last_cluster = 2
    for name83, file_bytes in files_to_place:
        cluster_count = max(1, math.ceil(len(file_bytes) / BYTES_PER_SECTOR))
        cluster_chain = [next_cluster + i * KERNEL_CLUSTER_STRIDE for i in range(cluster_count)]
        placed_files.append((name83, file_bytes, cluster_chain))
        next_cluster = cluster_chain[-1] + KERNEL_CLUSTER_STRIDE
        last_cluster = cluster_chain[-1]

    last_data_sector = data_start + (last_cluster - 2) + 1

    if last_data_sector > TOTAL_SECTORS:
        raise SystemExit("files do not fit in the FAT12 image")

    image = bytearray(TOTAL_SECTORS * BYTES_PER_SECTOR)
    image[0:BYTES_PER_SECTOR] = boot
    image[BYTES_PER_SECTOR:BYTES_PER_SECTOR + len(stage2)] = stage2

    fat = bytearray(SECTORS_PER_FAT * BYTES_PER_SECTOR)
    fat[0] = MEDIA_DESCRIPTOR
    fat[1] = 0xFF
    fat[2] = 0xFF

    for _, _, cluster_chain in placed_files:
        for i, cluster in enumerate(cluster_chain):
            next_value = 0xFFF if i == len(cluster_chain) - 1 else cluster_chain[i + 1]
            set_fat12_entry(fat, cluster, next_value)

    for fat_index in range(FAT_COUNT):
        offset = (reserved_sectors + fat_index * SECTORS_PER_FAT) * BYTES_PER_SECTOR
        image[offset:offset + len(fat)] = fat

    root_offset = root_start * BYTES_PER_SECTOR
    for index, (name83, file_bytes, cluster_chain) in enumerate(placed_files):
        entry = bytearray(32)
        entry[0:11] = name83
        entry[11] = 0x20
        struct.pack_into("<H", entry, 26, cluster_chain[0])
        struct.pack_into("<I", entry, 28, len(file_bytes))
        image[root_offset + index * 32:root_offset + (index + 1) * 32] = entry

        for i, cluster in enumerate(cluster_chain):
            chunk = file_bytes[i * BYTES_PER_SECTOR:(i + 1) * BYTES_PER_SECTOR]
            offset = (data_start + (cluster - 2)) * BYTES_PER_SECTOR
            image[offset:offset + len(chunk)] = chunk

    with open(args.output, "wb") as f:
        f.write(image)


if __name__ == "__main__":
    main()
