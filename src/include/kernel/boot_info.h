#ifndef KERNEL_BOOT_INFO_H
#define KERNEL_BOOT_INFO_H

#include <stdint.h>

#define BOOT_INFO_MAGIC 0x4649424D
#define BOOT_INFO_VERSION 1
#define BOOT_INFO_FLAG_UEFI 0x00000001
#define BOOT_INFO_FLAG_FRAMEBUFFER 0x00000002
#define BOOT_INFO_ADDR 0x8000
#define E820_MEMORY_MAP_ADDR 0x8200
#define E820_ENTRY_SIZE 24
#define E820_MAX_ENTRIES 128
#define BOOT_INFO_LEGACY_SIZE 48
#define BOOT_RESERVED_RANGE_MAX 8

#define BOOT_RESERVED_RANGE_KERNEL 1
#define BOOT_RESERVED_RANGE_BOOT_INFO 2
#define BOOT_RESERVED_RANGE_PAGE_TABLES 3
#define BOOT_RESERVED_RANGE_FRAMEBUFFER 4
#define BOOT_RESERVED_RANGE_KERNEL_STACK 5

typedef struct __attribute__((packed)) E820Entry {
    uint32_t base_low;
    uint32_t base_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
    uint32_t acpi_attrs;
} E820Entry;

typedef struct BootReservedRange {
    uint64_t base;
    uint64_t size;
    uint32_t type;
    uint32_t flags;
} BootReservedRange;

typedef struct BootInfo {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t boot_drive;
    uint32_t kernel_load_addr;
    uint32_t kernel_sector_count;
    uint32_t kernel_file_size;
    uint32_t stage2_load_addr;
    uint32_t memory_map_addr;
    uint32_t memory_map_entry_count;
    uint32_t memory_map_entry_size;
    uint32_t flags;
    uint64_t framebuffer_addr;
    uint64_t framebuffer_size;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pixels_per_scanline;
    uint32_t framebuffer_format;
    uint32_t reserved_range_count;
    uint32_t reserved_range_entry_size;
    BootReservedRange reserved_ranges[BOOT_RESERVED_RANGE_MAX];
} BootInfo;

#endif
