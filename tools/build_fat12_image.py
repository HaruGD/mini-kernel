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
KERNEL_LOAD_ADDR = 0x1000
STAGE2_LOAD_ADDR = 0x90000
KERNEL_CLUSTER_STRIDE = 2


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
    parser.add_argument("--output", required=True)
    parser.add_argument("--stage2-sectors", required=True, type=int)
    args = parser.parse_args()

    with open(args.boot, "rb") as f:
        boot = f.read()
    with open(args.stage2, "rb") as f:
        stage2 = f.read()
    with open(args.kernel, "rb") as f:
        kernel = f.read()

    if len(boot) != BYTES_PER_SECTOR:
        raise SystemExit("boot sector must be exactly 512 bytes")

    stage2_size = args.stage2_sectors * BYTES_PER_SECTOR
    if len(stage2) != stage2_size:
        raise SystemExit(f"stage2 must be padded to {stage2_size} bytes")

    if len(kernel) > STAGE2_LOAD_ADDR - KERNEL_LOAD_ADDR:
        raise SystemExit("kernel is too large for the current low-memory loader")

    reserved_sectors = 1 + args.stage2_sectors
    root_dir_sectors = (ROOT_ENTRY_COUNT * 32 + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
    root_start = reserved_sectors + FAT_COUNT * SECTORS_PER_FAT
    data_start = root_start + root_dir_sectors
    kernel_clusters = max(1, math.ceil(len(kernel) / BYTES_PER_SECTOR))
    kernel_cluster_chain = [2 + i * KERNEL_CLUSTER_STRIDE for i in range(kernel_clusters)]
    first_cluster = kernel_cluster_chain[0]
    last_data_sector = data_start + (kernel_cluster_chain[-1] - 2) + 1

    if last_data_sector > TOTAL_SECTORS:
        raise SystemExit("kernel does not fit in the FAT12 image")

    image = bytearray(TOTAL_SECTORS * BYTES_PER_SECTOR)
    image[0:BYTES_PER_SECTOR] = boot
    image[BYTES_PER_SECTOR:BYTES_PER_SECTOR + len(stage2)] = stage2

    fat = bytearray(SECTORS_PER_FAT * BYTES_PER_SECTOR)
    fat[0] = MEDIA_DESCRIPTOR
    fat[1] = 0xFF
    fat[2] = 0xFF

    for i, cluster in enumerate(kernel_cluster_chain):
        next_cluster = 0xFFF if i == kernel_clusters - 1 else kernel_cluster_chain[i + 1]
        set_fat12_entry(fat, cluster, next_cluster)

    for fat_index in range(FAT_COUNT):
        offset = (reserved_sectors + fat_index * SECTORS_PER_FAT) * BYTES_PER_SECTOR
        image[offset:offset + len(fat)] = fat

    root_offset = root_start * BYTES_PER_SECTOR
    entry = bytearray(32)
    entry[0:11] = b"KERNEL  BIN"
    entry[11] = 0x20
    struct.pack_into("<H", entry, 26, first_cluster)
    struct.pack_into("<I", entry, 28, len(kernel))
    image[root_offset:root_offset + 32] = entry

    for i, cluster in enumerate(kernel_cluster_chain):
        chunk = kernel[i * BYTES_PER_SECTOR:(i + 1) * BYTES_PER_SECTOR]
        offset = (data_start + (cluster - 2)) * BYTES_PER_SECTOR
        image[offset:offset + len(chunk)] = chunk

    with open(args.output, "wb") as f:
        f.write(image)


if __name__ == "__main__":
    main()
