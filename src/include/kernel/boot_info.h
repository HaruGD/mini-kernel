#ifndef KERNEL_BOOT_INFO_H
#define KERNEL_BOOT_INFO_H

#include <stdint.h>

#define BOOT_INFO_MAGIC 0x4649424D
#define BOOT_INFO_VERSION 1
#define BOOT_INFO_ADDR 0x8000
#define E820_MEMORY_MAP_ADDR 0x8200
#define E820_ENTRY_SIZE 24
#define E820_MAX_ENTRIES 16

typedef struct __attribute__((packed)) E820Entry {
    uint32_t base_low;
    uint32_t base_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
    uint32_t acpi_attrs;
} E820Entry;

typedef struct BootInfo {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t boot_drive;
    uint32_t kernel_load_addr;
    uint32_t kernel_sector_count;
    uint32_t stage2_load_addr;
    uint32_t memory_map_addr;
    uint32_t memory_map_entry_count;
    uint32_t memory_map_entry_size;
    uint32_t flags;
} BootInfo;

#endif
