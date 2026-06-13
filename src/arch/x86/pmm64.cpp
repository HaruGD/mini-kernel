#include "arch/x86/pmm64.h"
#include <stddef.h>

#define E820_TYPE_USABLE 1
#define LOW_MEMORY_RESERVE_SIZE (2 * 1024 * 1024)

static uint8_t memory_bitmap64[PMM64_BITMAP_SIZE];
static uint32_t free_blocks64 = 0;

static inline void mmap_set64(uint32_t bit) {
    memory_bitmap64[bit / 8] |= (1 << (bit % 8));
}

static inline void mmap_unset64(uint32_t bit) {
    memory_bitmap64[bit / 8] &= ~(1 << (bit % 8));
}

static inline int mmap_test64(uint32_t bit) {
    return memory_bitmap64[bit / 8] & (1 << (bit % 8));
}

static uint64_t align_up64(uint64_t value, uint64_t align) {
    return (value + align - 1) & ~(align - 1);
}

static uint64_t align_down64(uint64_t value, uint64_t align) {
    return value & ~(align - 1);
}

static void mark_all_used64() {
    for (uint32_t i = 0; i < PMM64_BITMAP_SIZE; i++) {
        memory_bitmap64[i] = 0xFF;
    }
    free_blocks64 = 0;
}

static void mark_block_used64(uint32_t index) {
    if (index >= PMM64_TOTAL_BLOCKS) {
        return;
    }
    if (!mmap_test64(index)) {
        mmap_set64(index);
        if (free_blocks64 > 0) {
            free_blocks64--;
        }
    }
}

static void mark_block_free64(uint32_t index) {
    if (index >= PMM64_TOTAL_BLOCKS) {
        return;
    }
    if (mmap_test64(index)) {
        mmap_unset64(index);
        free_blocks64++;
    }
}

static void mark_range_used64(uint64_t start, uint64_t end) {
    if (end <= start) {
        return;
    }

    uint64_t first_block = align_down64(start, PMM64_PAGE_SIZE) / PMM64_PAGE_SIZE;
    uint64_t last_block = align_up64(end, PMM64_PAGE_SIZE) / PMM64_PAGE_SIZE;

    if (last_block > PMM64_TOTAL_BLOCKS) {
        last_block = PMM64_TOTAL_BLOCKS;
    }

    for (uint64_t i = first_block; i < last_block; i++) {
        mark_block_used64((uint32_t)i);
    }
}

static void mark_range_size_used64(uint64_t start, uint64_t size) {
    if (size == 0) {
        return;
    }

    uint64_t end = start + size;
    if (end < start) {
        end = PMM64_MAX_RAM_SIZE;
    }
    mark_range_used64(start, end);
}

static void mark_range_free64(uint64_t start, uint64_t end) {
    uint64_t first_block = align_up64(start, PMM64_PAGE_SIZE) / PMM64_PAGE_SIZE;
    uint64_t last_block = align_down64(end, PMM64_PAGE_SIZE) / PMM64_PAGE_SIZE;

    if (first_block >= PMM64_TOTAL_BLOCKS) {
        return;
    }
    if (last_block > PMM64_TOTAL_BLOCKS) {
        last_block = PMM64_TOTAL_BLOCKS;
    }
    if (first_block >= last_block) {
        return;
    }

    for (uint64_t i = first_block; i < last_block; i++) {
        mark_block_free64((uint32_t)i);
    }
}

static uint64_t e820_entry_end64(const E820Entry* entry) {
    uint64_t base = ((uint64_t)entry->base_high << 32) | entry->base_low;
    uint64_t length = ((uint64_t)entry->length_high << 32) | entry->length_low;
    uint64_t end = base + length;

    if (end < base || end > PMM64_MAX_RAM_SIZE) {
        end = PMM64_MAX_RAM_SIZE;
    }
    return end;
}

static uint32_t kernel_reserved_size64(const BootInfo* boot_info) {
    if (boot_info->kernel_file_size != 0) {
        return boot_info->kernel_file_size;
    }
    if (boot_info->kernel_sector_count != 0) {
        return boot_info->kernel_sector_count * 512U;
    }
    return 0;
}

static void reserve_boot_info_ranges64(const BootInfo* boot_info) {
    uint32_t kernel_size = kernel_reserved_size64(boot_info);
    if (boot_info->kernel_load_addr != 0 && kernel_size != 0) {
        mark_range_size_used64(boot_info->kernel_load_addr, kernel_size);
    }

    if (boot_info->size < sizeof(BootInfo) ||
        boot_info->reserved_range_entry_size < sizeof(BootReservedRange)) {
        return;
    }

    uint32_t count = boot_info->reserved_range_count;
    if (count > BOOT_RESERVED_RANGE_MAX) {
        count = BOOT_RESERVED_RANGE_MAX;
    }

    for (uint32_t i = 0; i < count; i++) {
        const BootReservedRange* range = &boot_info->reserved_ranges[i];
        mark_range_size_used64(range->base, range->size);
    }
}

static int init_from_e820_64(const BootInfo* boot_info) {
    if (!boot_info || boot_info->magic != BOOT_INFO_MAGIC || boot_info->memory_map_entry_count == 0) {
        return 0;
    }

    const E820Entry* entries = (const E820Entry*)(uintptr_t)boot_info->memory_map_addr;
    uint32_t count = boot_info->memory_map_entry_count;
    if (count > E820_MAX_ENTRIES) {
        count = E820_MAX_ENTRIES;
    }

    mark_all_used64();

    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].type != E820_TYPE_USABLE) {
            continue;
        }

        uint64_t base = ((uint64_t)entries[i].base_high << 32) | entries[i].base_low;
        uint64_t end = e820_entry_end64(&entries[i]);
        if (base >= PMM64_MAX_RAM_SIZE) {
            continue;
        }
        mark_range_free64(base, end);
    }

    mark_range_used64(0, LOW_MEMORY_RESERVE_SIZE);
    reserve_boot_info_ranges64(boot_info);
    return 1;
}

static void init_fallback64() {
    for (uint32_t i = 0; i < PMM64_BITMAP_SIZE; i++) {
        memory_bitmap64[i] = 0;
    }
    free_blocks64 = PMM64_TOTAL_BLOCKS;
    mark_range_used64(0, LOW_MEMORY_RESERVE_SIZE);
}

extern "C" void pmm64_init(const BootInfo* boot_info) {
    if (!init_from_e820_64(boot_info)) {
        init_fallback64();
    }
}

extern "C" void* pmm64_alloc_block() {
    for (uint32_t i = 0; i < PMM64_TOTAL_BLOCKS; i++) {
        if (!mmap_test64(i)) {
            mark_block_used64(i);
            return (void*)((uintptr_t)i * PMM64_PAGE_SIZE);
        }
    }
    return 0;
}

extern "C" void* pmm64_alloc_blocks(uint32_t count) {
    if (count == 0) {
        return 0;
    }

    uint32_t run_start = 0;
    uint32_t run_length = 0;

    for (uint32_t i = 0; i < PMM64_TOTAL_BLOCKS; i++) {
        if (!mmap_test64(i)) {
            if (run_length == 0) {
                run_start = i;
            }
            run_length++;
            if (run_length == count) {
                for (uint32_t j = 0; j < count; j++) {
                    mark_block_used64(run_start + j);
                }
                return (void*)((uintptr_t)run_start * PMM64_PAGE_SIZE);
            }
        } else {
            run_length = 0;
        }
    }

    return 0;
}

extern "C" void pmm64_free_block(void* addr) {
    uintptr_t block = (uintptr_t)addr / PMM64_PAGE_SIZE;
    mark_block_free64((uint32_t)block);
}

extern "C" void pmm64_free_blocks(void* addr, uint32_t count) {
    uintptr_t block = (uintptr_t)addr / PMM64_PAGE_SIZE;
    for (uint32_t i = 0; i < count; i++) {
        mark_block_free64((uint32_t)(block + i));
    }
}

extern "C" uint32_t pmm64_get_total_block_count() {
    return PMM64_TOTAL_BLOCKS;
}

extern "C" uint32_t pmm64_get_free_block_count() {
    return free_blocks64;
}

extern "C" int pmm64_range_is_marked_used(uint64_t start, uint64_t size) {
    if (size == 0) {
        return 1;
    }

    uint64_t end = start + size;
    if (end < start) {
        end = PMM64_MAX_RAM_SIZE;
    }
    if (start >= PMM64_MAX_RAM_SIZE) {
        return 1;
    }
    if (end > PMM64_MAX_RAM_SIZE) {
        end = PMM64_MAX_RAM_SIZE;
    }

    uint64_t first_block = align_down64(start, PMM64_PAGE_SIZE) / PMM64_PAGE_SIZE;
    uint64_t last_block = align_up64(end, PMM64_PAGE_SIZE) / PMM64_PAGE_SIZE;
    if (last_block > PMM64_TOTAL_BLOCKS) {
        last_block = PMM64_TOTAL_BLOCKS;
    }

    for (uint64_t i = first_block; i < last_block; i++) {
        if (!mmap_test64((uint32_t)i)) {
            return 0;
        }
    }
    return 1;
}
