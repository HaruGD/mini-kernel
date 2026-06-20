#include "heap.h"
#include "arch/x86_64/paging64.h"
#include "arch/x86_64/pmm64.h"

#define HEAP_BLOCK_MAGIC 0x48454150U
#define HEAP_FREE_BIN_COUNT 8

typedef char heap_header_alignment_check[(sizeof(struct heap_header) % 16 == 0) ? 1 : -1];

static struct heap_header* heap_free_bins[HEAP_FREE_BIN_COUNT];
static struct heap_header* heap_tail = 0;
static uint64_t heap_next_virtual = PAGING64_KERNEL_HEAP_BASE;
static uint32_t heap_mapped_pages = 0;
static uint64_t heap_alloc_requests = 0;
static uint64_t heap_free_requests = 0;
static uint64_t heap_alloc_failures = 0;
static uint64_t heap_invalid_free_requests = 0;
static uint64_t heap_double_free_requests = 0;
static uint64_t heap_free_list_hits = 0;
static uint64_t heap_free_list_misses = 0;
static uint64_t heap_current_used_bytes = 0;
static uint64_t heap_peak_used_bytes = 0;
static uint32_t heap_grow_requests = 0;

static uintptr_t align_up_heap(uintptr_t value, uintptr_t align) {
    return (value + align - 1) & ~(align - 1);
}

static uintptr_t align_down_heap(uintptr_t value, uintptr_t align) {
    return value & ~(align - 1);
}

struct heap_header* heap_start = 0;

static int heap_block_valid(const struct heap_header* block) {
    return block != 0 && block->magic == HEAP_BLOCK_MAGIC;
}

static uint32_t heap_bin_index(uint32_t size) {
    if (size <= 64) return 0;
    if (size <= 128) return 1;
    if (size <= 256) return 2;
    if (size <= 512) return 3;
    if (size <= 1024) return 4;
    if (size <= 2048) return 5;
    if (size <= 4096) return 6;
    return 7;
}

static void heap_reset_free_bins() {
    for (uint32_t i = 0; i < HEAP_FREE_BIN_COUNT; i++) {
        heap_free_bins[i] = 0;
    }
}

static void heap_init_block(struct heap_header* block,
                            uint32_t size,
                            uint8_t is_free,
                            struct heap_header* next,
                            struct heap_header* prev) {
    block->magic = HEAP_BLOCK_MAGIC;
    block->size = size;
    block->requested_size = 0;
    block->is_free = is_free;
    block->in_free_list = 0;
    block->next = next;
    block->prev = prev;
    block->free_next = 0;
    block->free_prev = 0;
    block->reserved[0] = 0;
    block->reserved[1] = 0;
}

static void heap_remove_free_block(struct heap_header* block) {
    if (!heap_block_valid(block) || !block->in_free_list) {
        return;
    }

    uint32_t bin = heap_bin_index(block->size);
    if (block->free_prev != 0) {
        block->free_prev->free_next = block->free_next;
    } else if (bin < HEAP_FREE_BIN_COUNT && heap_free_bins[bin] == block) {
        heap_free_bins[bin] = block->free_next;
    }
    if (block->free_next != 0) {
        block->free_next->free_prev = block->free_prev;
    }

    block->free_next = 0;
    block->free_prev = 0;
    block->in_free_list = 0;
}

static void heap_insert_free_block(struct heap_header* block) {
    if (!heap_block_valid(block) || !block->is_free || block->in_free_list) {
        return;
    }

    uint32_t bin = heap_bin_index(block->size);
    block->free_prev = 0;
    block->free_next = heap_free_bins[bin];
    if (heap_free_bins[bin] != 0) {
        heap_free_bins[bin]->free_prev = block;
    }
    heap_free_bins[bin] = block;
    block->in_free_list = 1;
}

static struct heap_header* heap_find_free_block(uint32_t total_size) {
    uint32_t bin = heap_bin_index(total_size);
    for (uint32_t i = bin; i < HEAP_FREE_BIN_COUNT; i++) {
        struct heap_header* current = heap_free_bins[i];
        while (current != 0) {
            if (heap_block_valid(current) && current->is_free && current->size >= total_size) {
                heap_free_list_hits++;
                return current;
            }
            current = current->free_next;
        }
    }
    heap_free_list_misses++;
    return 0;
}

static void heap_update_peak_used() {
    if (heap_current_used_bytes > heap_peak_used_bytes) {
        heap_peak_used_bytes = heap_current_used_bytes;
    }
}

static void heap_mark_allocated(struct heap_header* block, uint32_t requested_size) {
    block->is_free = 0;
    block->requested_size = requested_size;
    heap_current_used_bytes += requested_size;
    heap_update_peak_used();
}

static void heap_mark_free(struct heap_header* block) {
    if (heap_current_used_bytes >= block->requested_size) {
        heap_current_used_bytes -= block->requested_size;
    } else {
        heap_current_used_bytes = 0;
    }
    block->is_free = 1;
    block->requested_size = 0;
}

static void heap_zero_region(uint64_t region, uint64_t size) {
    uint64_t qword_count = size / sizeof(uint64_t);
    uint64_t* qwords = (uint64_t*)(uintptr_t)region;
    for (uint64_t i = 0; i < qword_count; i++) {
        qwords[i] = 0;
    }

    uint64_t byte_start = qword_count * sizeof(uint64_t);
    uint8_t* bytes = (uint8_t*)(uintptr_t)(region + byte_start);
    for (uint64_t i = byte_start; i < size; i++) {
        bytes[i - byte_start] = 0;
    }
}

static void split_block(struct heap_header* block, uint32_t total_size) {
    if (block->size < total_size + sizeof(struct heap_header) + 16) {
        return;
    }

    struct heap_header* next_block = (struct heap_header*)((uintptr_t)block + total_size);
    heap_remove_free_block(block);
    heap_init_block(next_block, block->size - total_size, 1, block->next, block);
    if (next_block->next != 0) {
        next_block->next->prev = next_block;
    }

    block->size = total_size;
    block->next = next_block;

    if (heap_tail == block) {
        heap_tail = next_block;
    }
    heap_insert_free_block(next_block);
}

static int heap_blocks_adjacent(const struct heap_header* left, const struct heap_header* right) {
    return heap_block_valid(left) &&
           heap_block_valid(right) &&
           ((uintptr_t)left + left->size) == (uintptr_t)right;
}

static struct heap_header* merge_with_next(struct heap_header* block) {
    if (block == 0 || block->next == 0) {
        return block;
    }
    struct heap_header* next = block->next;
    if (!block->is_free || !next->is_free || !heap_blocks_adjacent(block, next)) {
        return block;
    }

    heap_remove_free_block(block);
    heap_remove_free_block(next);
    block->size += next->size;
    block->requested_size = 0;
    block->next = next->next;
    if (block->next != 0) {
        block->next->prev = block;
    }
    if (heap_tail == next) {
        heap_tail = block;
    }
    heap_insert_free_block(block);
    return block;
}

static struct heap_header* coalesce_around(struct heap_header* block) {
    if (block == 0 || !heap_block_valid(block) || !block->is_free) {
        return block;
    }

    heap_insert_free_block(block);
    if (block->prev != 0 &&
        block->prev->is_free &&
        heap_blocks_adjacent(block->prev, block)) {
        block = merge_with_next(block->prev);
    }

    return merge_with_next(block);
}

static struct heap_header* append_region(uint32_t bytes) {
    heap_grow_requests++;
    uint32_t page_count = (bytes + PMM64_PAGE_SIZE - 1) / PMM64_PAGE_SIZE;
    uint64_t region = heap_next_virtual;
    uint64_t region_size = (uint64_t)page_count * PMM64_PAGE_SIZE;

    if (region + region_size > PAGING64_KERNEL_HEAP_LIMIT) {
        return 0;
    }

    uint32_t mapped_pages = 0;
    if (!paging64_alloc_map_range(region,
                                  region_size,
                                  PAGING64_FLAG_WRITABLE | PAGING64_FLAG_GLOBAL | PAGING64_FLAG_NX,
                                  &mapped_pages)) {
        return 0;
    }
    heap_mapped_pages += mapped_pages;

    heap_next_virtual += region_size;

    heap_zero_region(region, region_size);

    struct heap_header* block = (struct heap_header*)(uintptr_t)region;
    heap_init_block(block, page_count * PMM64_PAGE_SIZE, 1, 0, heap_tail);

    if (heap_start == 0) {
        heap_start = block;
        heap_tail = block;
    } else {
        heap_tail->next = block;
        heap_tail = block;
    }
    heap_insert_free_block(block);

    return block;
}

static void shrink_heap_tail() {
    while (heap_tail != 0 && heap_tail->is_free) {
        uintptr_t block_start = (uintptr_t)heap_tail;
        uintptr_t block_end = block_start + heap_tail->size;

        if (block_end != heap_next_virtual) {
            break;
        }

        uintptr_t releasable_start = align_up_heap(block_start + sizeof(struct heap_header), PMM64_PAGE_SIZE);
        if (block_start == align_down_heap(block_start, PMM64_PAGE_SIZE) &&
            heap_tail->size >= PMM64_PAGE_SIZE) {
            releasable_start = block_start;
        }

        if (releasable_start >= block_end) {
            break;
        }

        heap_remove_free_block(heap_tail);
        uint32_t page_count = (uint32_t)((block_end - releasable_start) / PMM64_PAGE_SIZE);
        uint32_t unmapped = paging64_unmap_free_range((uint64_t)releasable_start, page_count);
        if (unmapped > heap_mapped_pages) {
            heap_mapped_pages = 0;
        } else {
            heap_mapped_pages -= unmapped;
        }

        heap_next_virtual = releasable_start;

        if (releasable_start == block_start) {
            struct heap_header* prev = heap_tail->prev;
            if (prev != 0) {
                prev->next = 0;
            } else {
                heap_start = 0;
            }
            heap_tail = prev;
        } else {
            heap_tail->size = releasable_start - block_start;
            heap_tail->next = 0;
            heap_insert_free_block(heap_tail);
            break;
        }
    }
}

extern "C" void heap_init() {
    heap_start = 0;
    heap_tail = 0;
    heap_reset_free_bins();
    heap_next_virtual = PAGING64_KERNEL_HEAP_BASE;
    heap_mapped_pages = 0;
    heap_alloc_requests = 0;
    heap_free_requests = 0;
    heap_alloc_failures = 0;
    heap_invalid_free_requests = 0;
    heap_double_free_requests = 0;
    heap_free_list_hits = 0;
    heap_free_list_misses = 0;
    heap_current_used_bytes = 0;
    heap_peak_used_bytes = 0;
    heap_grow_requests = 0;
    append_region(PMM64_PAGE_SIZE * 4);
    heap_update_peak_used();
}

extern "C" void* kmalloc(size_t size) {
    heap_alloc_requests++;
    if (size == 0) {
        heap_alloc_failures++;
        return 0;
    }
    if (size > 0xFFFFFFFFULL - sizeof(struct heap_header)) {
        heap_alloc_failures++;
        return 0;
    }

    uint32_t total_size = (uint32_t)align_up_heap(size + sizeof(struct heap_header), 16);
    struct heap_header* current = heap_find_free_block(total_size);
    if (current != 0) {
        split_block(current, total_size);
        heap_remove_free_block(current);
        heap_mark_allocated(current, (uint32_t)size);
        return (void*)((uintptr_t)current + sizeof(struct heap_header));
    }

    uint32_t grow_size = total_size;
    if (grow_size < PMM64_PAGE_SIZE * 4) {
        grow_size = PMM64_PAGE_SIZE * 4;
    }

    current = append_region(grow_size);
    if (current == 0) {
        heap_alloc_failures++;
        return 0;
    }

    split_block(current, total_size);
    heap_remove_free_block(current);
    heap_mark_allocated(current, (uint32_t)size);
    return (void*)((uintptr_t)current + sizeof(struct heap_header));
}

extern "C" void heap_coalesce() {
    struct heap_header* curr = heap_start;
    while (curr != 0 && curr->next != 0) {
        uintptr_t curr_end = (uintptr_t)curr + curr->size;
        uintptr_t next_start = (uintptr_t)curr->next;

        if (heap_block_valid(curr) &&
            heap_block_valid(curr->next) &&
            curr->is_free &&
            curr->next->is_free &&
            curr_end == next_start) {
            heap_remove_free_block(curr);
            heap_remove_free_block(curr->next);
            curr->size += curr->next->size;
            curr->requested_size = 0;
            if (heap_tail == curr->next) {
                heap_tail = curr;
            }
            curr->next = curr->next->next;
            if (curr->next != 0) {
                curr->next->prev = curr;
            }
            heap_insert_free_block(curr);
        } else {
            curr = curr->next;
        }
    }
}

extern "C" void kfree(void* ptr) {
    if (ptr == 0) {
        return;
    }

    heap_free_requests++;
    struct heap_header* header = (struct heap_header*)((uintptr_t)ptr - sizeof(struct heap_header));
    if (!heap_block_valid(header)) {
        heap_invalid_free_requests++;
        return;
    }
    if (header->is_free) {
        heap_double_free_requests++;
        return;
    }
    heap_mark_free(header);
    coalesce_around(header);
    shrink_heap_tail();
}

extern "C" uint64_t heap_total_free() {
    uint64_t total = 0;
    struct heap_header* current = heap_start;
    while (current != 0) {
        if (heap_block_valid(current) && current->is_free && current->size >= sizeof(struct heap_header)) {
            total += current->size - sizeof(struct heap_header);
        }
        current = current->next;
    }
    return total;
}

extern "C" uint64_t heap_total_used() {
    return heap_current_used_bytes;
}

extern "C" uint64_t heap_total_mapped_bytes() {
    return (uint64_t)heap_mapped_pages * PMM64_PAGE_SIZE;
}

extern "C" uint32_t heap_mapped_page_count() {
    return heap_mapped_pages;
}

extern "C" uint32_t heap_region_count() {
    uint32_t count = 0;
    struct heap_header* current = heap_start;
    while (current != 0) {
        count++;
        current = current->next;
    }
    return count;
}

extern "C" void heap_get_stats(HeapStats* out_stats) {
    if (out_stats == 0) {
        return;
    }

    uint64_t largest_free = 0;
    struct heap_header* current = heap_start;
    while (current != 0) {
        if (heap_block_valid(current) && current->is_free && current->size >= sizeof(struct heap_header)) {
            uint64_t free_bytes = current->size - sizeof(struct heap_header);
            if (free_bytes > largest_free) {
                largest_free = free_bytes;
            }
        }
        current = current->next;
    }

    out_stats->used_bytes = heap_current_used_bytes;
    out_stats->free_bytes = heap_total_free();
    out_stats->mapped_bytes = heap_total_mapped_bytes();
    out_stats->peak_used_bytes = heap_peak_used_bytes;
    out_stats->largest_free_bytes = largest_free;
    out_stats->alloc_requests = heap_alloc_requests;
    out_stats->free_requests = heap_free_requests;
    out_stats->alloc_failures = heap_alloc_failures;
    out_stats->invalid_free_requests = heap_invalid_free_requests;
    out_stats->double_free_requests = heap_double_free_requests;
    out_stats->free_list_hits = heap_free_list_hits;
    out_stats->free_list_misses = heap_free_list_misses;
    out_stats->free_bin_count = HEAP_FREE_BIN_COUNT;
    out_stats->mapped_pages = heap_mapped_pages;
    out_stats->region_count = heap_region_count();
    out_stats->grow_requests = heap_grow_requests;
}

inline void* operator new(size_t, void* ptr) { return ptr; }
inline void* operator new[](size_t, void* ptr) { return ptr; }

void* operator new(size_t size) { return kmalloc(size); }
void* operator new[](size_t size) { return kmalloc(size); }
void operator delete(void* ptr) { kfree(ptr); }
void operator delete[](void* ptr) { kfree(ptr); }
void operator delete(void* ptr, size_t) { kfree(ptr); }
void operator delete[](void* ptr, size_t) { kfree(ptr); }

extern "C" void __cxa_pure_virtual() {
    while (1) {
        __asm__ volatile("cli; hlt");
    }
}
