#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define PAGE_SIZE 4096
#define BLOCKS_PER_BYTE 8

// 128MB RAM 기준 (필요에 따라 조절)
#define MAX_RAM_SIZE (128 * 1024 * 1024)
#define TOTAL_BLOCKS (MAX_RAM_SIZE / PAGE_SIZE)
#define BITMAP_SIZE (TOTAL_BLOCKS / BLOCKS_PER_BYTE)

void pmm_init();
void* pmm_alloc_block();
void pmm_free_block(void* addr);

#endif