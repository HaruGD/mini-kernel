#ifndef KERNEL_BOOT_INFO_H
#define KERNEL_BOOT_INFO_H

#include <stdint.h>

#define BOOT_INFO_MAGIC 0x4649424D
#define BOOT_INFO_VERSION 1
#define BOOT_INFO_ADDR 0x8000

typedef struct BootInfo {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t boot_drive;
    uint32_t kernel_load_addr;
    uint32_t kernel_sector_count;
    uint32_t stage2_load_addr;
    uint32_t flags;
} BootInfo;

#endif
