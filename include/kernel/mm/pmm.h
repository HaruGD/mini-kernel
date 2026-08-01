#ifndef KERNEL_MM_PMM_H
#define KERNEL_MM_PMM_H

#include <stdint.h>
#include "kernel/boot_info.h"

#define PMM_PAGE_SIZE 4096
#define PMM_MAX_RAM_SIZE (512 * 1024 * 1024)
#define PMM_TOTAL_BLOCKS (PMM_MAX_RAM_SIZE / PMM_PAGE_SIZE)
#define PMM_BITMAP_SIZE (PMM_TOTAL_BLOCKS / 8)

typedef struct PmmStats {
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t next_free_hint;
    uint64_t alloc_requests;
    uint64_t alloc_contiguous_requests;
    uint64_t alloc_failures;
    uint64_t alloc_scan_steps;
    uint64_t free_requests;
    uint64_t peak_used_blocks;
} PmmStats;

#ifdef __cplusplus
extern "C" {
#endif

void pmm_init(const BootInfo* boot_info);
void* pmm_alloc_block();
void* pmm_alloc_blocks(uint32_t count);
void* pmm_alloc_blocks_constrained(uint32_t count, uint32_t alignment_blocks,
                                   uint64_t boundary_bytes,
                                   uint64_t maximum_address);
void pmm_free_block(void* addr);
void pmm_free_blocks(void* addr, uint32_t count);
uint32_t pmm_get_total_block_count();
uint32_t pmm_get_free_block_count();
void pmm_get_stats(PmmStats* out_stats);
int pmm_range_is_marked_used(uint64_t start, uint64_t size);

#ifdef __cplusplus
}
#endif

#endif
