#include "arch/x86/pmm.h"

// 메모리 상태를 기록할 비트맵 배열
uint8_t memory_bitmap[BITMAP_SIZE];

#define E820_TYPE_USABLE 1
#define LOW_MEMORY_RESERVE_SIZE (2 * 1024 * 1024)

// 비트맵에서 특정 비트를 1로 설정 (사용 중)
static inline void mmap_set(int bit) {
    memory_bitmap[bit / 8] |= (1 << (bit % 8));
}

// 비트맵에서 특정 비트를 0으로 설정 (비어 있음)
static inline void mmap_unset(int bit) {
    memory_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static uint32_t align_up(uint32_t value, uint32_t align) {
    return (value + align - 1) & ~(align - 1);
}

static uint32_t align_down(uint32_t value, uint32_t align) {
    return value & ~(align - 1);
}

static void mark_all_used() {
    for (int i = 0; i < BITMAP_SIZE; i++) {
        memory_bitmap[i] = 0xFF;
    }
}

static void mark_range_used(uint32_t start, uint32_t end) {
    uint32_t first_block = align_down(start, PAGE_SIZE) / PAGE_SIZE;
    uint32_t last_block = align_up(end, PAGE_SIZE) / PAGE_SIZE;

    if (last_block > TOTAL_BLOCKS) {
        last_block = TOTAL_BLOCKS;
    }

    for (uint32_t i = first_block; i < last_block; i++) {
        mmap_set(i);
    }
}

static void mark_range_free(uint32_t start, uint32_t end) {
    uint32_t first_block = align_up(start, PAGE_SIZE) / PAGE_SIZE;
    uint32_t last_block = align_down(end, PAGE_SIZE) / PAGE_SIZE;

    if (first_block >= TOTAL_BLOCKS) {
        return;
    }
    if (last_block > TOTAL_BLOCKS) {
        last_block = TOTAL_BLOCKS;
    }
    if (first_block >= last_block) {
        return;
    }

    for (uint32_t i = first_block; i < last_block; i++) {
        mmap_unset(i);
    }
}

static uint32_t e820_entry_end(const E820Entry* entry) {
    uint32_t base = entry->base_low;
    uint32_t length = entry->length_low;
    uint32_t end = base + length;

    if (entry->base_high != 0 || entry->length_high != 0 || end < base || end > MAX_RAM_SIZE) {
        return MAX_RAM_SIZE;
    }

    return end;
}

static int init_from_e820(const BootInfo* boot_info) {
    if (!boot_info || boot_info->magic != BOOT_INFO_MAGIC || boot_info->memory_map_entry_count == 0) {
        return 0;
    }

    const E820Entry* entries = (const E820Entry*)boot_info->memory_map_addr;
    uint32_t count = boot_info->memory_map_entry_count;

    if (count > E820_MAX_ENTRIES) {
        count = E820_MAX_ENTRIES;
    }

    mark_all_used();

    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].type != E820_TYPE_USABLE || entries[i].base_high != 0) {
            continue;
        }

        uint32_t start = entries[i].base_low;
        uint32_t end = e820_entry_end(&entries[i]);
        mark_range_free(start, end);
    }

    mark_range_used(0, LOW_MEMORY_RESERVE_SIZE);
    return 1;
}

static void init_fallback() {
    for (int i = 0; i < BITMAP_SIZE; i++) {
        memory_bitmap[i] = 0;
    }

    mark_range_used(0, LOW_MEMORY_RESERVE_SIZE);
}

// 비트맵 초기화
void pmm_init(const BootInfo* boot_info) {
    if (!init_from_e820(boot_info)) {
        init_fallback();
    }
}

// 비어있는 메모리 블록(4KB) 하나를 찾아서 주소를 반환
void* pmm_alloc_block() {
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        // 비트가 0인 곳(비어있는 곳)을 발견하면
        if (!(memory_bitmap[i / 8] & (1 << (i % 8)))) {
            mmap_set(i); // 사용 중으로 표시
            return (void*)(i * PAGE_SIZE); // 실제 메모리 주소로 변환하여 반환
        }
    }
    return 0; // 메모리 부족 시 NULL 반환
}

// 사용이 끝난 메모리 블록을 다시 비움
void pmm_free_block(void* addr) {
    int block = (uint32_t)addr / PAGE_SIZE;
    mmap_unset(block);
}
