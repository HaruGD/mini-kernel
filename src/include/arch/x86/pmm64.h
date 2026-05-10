#ifndef PMM64_H
#define PMM64_H

#include <stdint.h>
#include "kernel/boot_info.h"

#define PMM64_PAGE_SIZE 4096
#define PMM64_MAX_RAM_SIZE (128 * 1024 * 1024)
#define PMM64_TOTAL_BLOCKS (PMM64_MAX_RAM_SIZE / PMM64_PAGE_SIZE)
#define PMM64_BITMAP_SIZE (PMM64_TOTAL_BLOCKS / 8)

#ifdef __cplusplus
extern "C" {
#endif

void pmm64_init(const BootInfo* boot_info);
void* pmm64_alloc_block();
void* pmm64_alloc_blocks(uint32_t count);
void pmm64_free_block(void* addr);
void pmm64_free_blocks(void* addr, uint32_t count);
uint32_t pmm64_get_total_block_count();
uint32_t pmm64_get_free_block_count();

#ifdef __cplusplus
}
#endif

#endif
