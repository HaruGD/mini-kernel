#include "heap.h"
#include "arch/x86/paging64.h"
#include "arch/x86/pmm64.h"

static struct heap_header* heap_tail = 0;
static uint64_t heap_next_virtual = PAGING64_KERNEL_HEAP_BASE;
static uint32_t heap_mapped_pages = 0;

static uintptr_t align_up_heap(uintptr_t value, uintptr_t align) {
    return (value + align - 1) & ~(align - 1);
}

static uintptr_t align_down_heap(uintptr_t value, uintptr_t align) {
    return value & ~(align - 1);
}

struct heap_header* heap_start = 0;

static struct heap_header* find_prev_block(struct heap_header* target) {
    if (target == 0 || target == heap_start) {
        return 0;
    }

    struct heap_header* current = heap_start;
    while (current != 0 && current->next != target) {
        current = current->next;
    }
    return current;
}

static void split_block(struct heap_header* block, uint32_t total_size) {
    if (block->size < total_size + sizeof(struct heap_header) + 16) {
        return;
    }

    struct heap_header* next_block = (struct heap_header*)((uintptr_t)block + total_size);
    next_block->size = block->size - total_size;
    next_block->is_free = 1;
    next_block->next = block->next;

    block->size = total_size;
    block->next = next_block;

    if (heap_tail == block) {
        heap_tail = next_block;
    }
}

static struct heap_header* append_region(uint32_t bytes) {
    uint32_t page_count = (bytes + PMM64_PAGE_SIZE - 1) / PMM64_PAGE_SIZE;
    uint64_t region = heap_next_virtual;
    uint64_t region_size = (uint64_t)page_count * PMM64_PAGE_SIZE;

    if (region + region_size > PAGING64_KERNEL_HEAP_LIMIT) {
        return 0;
    }

    for (uint32_t i = 0; i < page_count; i++) {
        uint64_t virt = region + ((uint64_t)i * PMM64_PAGE_SIZE);
        uint64_t phys = (uint64_t)(uintptr_t)pmm64_alloc_block();
        if (phys == 0) {
            for (uint32_t rollback = 0; rollback < i; rollback++) {
                uint64_t rollback_virt = region + ((uint64_t)rollback * PMM64_PAGE_SIZE);
                uint64_t rollback_phys = paging64_get_phys(rollback_virt) & 0x000FFFFFFFFFF000ULL;
                paging64_unmap_page(rollback_virt);
                if (rollback_phys != 0) {
                    pmm64_free_block((void*)(uintptr_t)rollback_phys);
                    if (heap_mapped_pages > 0) {
                        heap_mapped_pages--;
                    }
                }
            }
            return 0;
        }

        if (!paging64_map_page(virt, phys, PAGING64_FLAG_WRITABLE | PAGING64_FLAG_GLOBAL)) {
            pmm64_free_block((void*)(uintptr_t)phys);
            for (uint32_t rollback = 0; rollback < i; rollback++) {
                uint64_t rollback_virt = region + ((uint64_t)rollback * PMM64_PAGE_SIZE);
                uint64_t rollback_phys = paging64_get_phys(rollback_virt) & 0x000FFFFFFFFFF000ULL;
                paging64_unmap_page(rollback_virt);
                if (rollback_phys != 0) {
                    pmm64_free_block((void*)(uintptr_t)rollback_phys);
                    if (heap_mapped_pages > 0) {
                        heap_mapped_pages--;
                    }
                }
            }
            return 0;
        }

        heap_mapped_pages++;
    }

    heap_next_virtual += region_size;

    for (uint64_t i = 0; i < region_size; i++) {
        *((volatile uint8_t*)(uintptr_t)(region + i)) = 0;
    }

    struct heap_header* block = (struct heap_header*)(uintptr_t)region;
    block->size = page_count * PMM64_PAGE_SIZE;
    block->is_free = 1;
    block->next = 0;

    if (heap_start == 0) {
        heap_start = block;
        heap_tail = block;
    } else {
        heap_tail->next = block;
        heap_tail = block;
    }

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

        for (uintptr_t virt = releasable_start; virt < block_end; virt += PMM64_PAGE_SIZE) {
            uint64_t phys = paging64_get_phys((uint64_t)virt) & 0x000FFFFFFFFFF000ULL;
            if (phys != 0) {
                paging64_unmap_page((uint64_t)virt);
                pmm64_free_block((void*)(uintptr_t)phys);
                if (heap_mapped_pages > 0) {
                    heap_mapped_pages--;
                }
            }
        }

        heap_next_virtual = releasable_start;

        if (releasable_start == block_start) {
            struct heap_header* prev = find_prev_block(heap_tail);
            if (prev != 0) {
                prev->next = 0;
            } else {
                heap_start = 0;
            }
            heap_tail = prev;
        } else {
            heap_tail->size = releasable_start - block_start;
            heap_tail->next = 0;
            break;
        }
    }
}

extern "C" void heap_init() {
    heap_start = 0;
    heap_tail = 0;
    heap_next_virtual = PAGING64_KERNEL_HEAP_BASE;
    heap_mapped_pages = 0;
    append_region(PMM64_PAGE_SIZE * 4);
}

extern "C" void* kmalloc(size_t size) {
    if (size == 0) {
        return 0;
    }

    uint32_t total_size = (uint32_t)align_up_heap(size + sizeof(struct heap_header), 16);
    struct heap_header* current = heap_start;

    while (current != 0) {
        if (current->is_free && current->size >= total_size) {
            split_block(current, total_size);
            current->is_free = 0;
            return (void*)((uintptr_t)current + sizeof(struct heap_header));
        }
        current = current->next;
    }

    uint32_t grow_size = total_size;
    if (grow_size < PMM64_PAGE_SIZE * 4) {
        grow_size = PMM64_PAGE_SIZE * 4;
    }

    current = append_region(grow_size);
    if (current == 0) {
        return 0;
    }

    split_block(current, total_size);
    current->is_free = 0;
    return (void*)((uintptr_t)current + sizeof(struct heap_header));
}

extern "C" void heap_coalesce() {
    struct heap_header* curr = heap_start;
    while (curr != 0 && curr->next != 0) {
        uintptr_t curr_end = (uintptr_t)curr + curr->size;
        uintptr_t next_start = (uintptr_t)curr->next;

        if (curr->is_free && curr->next->is_free && curr_end == next_start) {
            curr->size += curr->next->size;
            if (heap_tail == curr->next) {
                heap_tail = curr;
            }
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

extern "C" void kfree(void* ptr) {
    if (ptr == 0) {
        return;
    }

    struct heap_header* header = (struct heap_header*)((uintptr_t)ptr - sizeof(struct heap_header));
    header->is_free = 1;
    heap_coalesce();
    shrink_heap_tail();
}

extern "C" uint64_t heap_total_free() {
    uint64_t total = 0;
    struct heap_header* current = heap_start;
    while (current != 0) {
        if (current->is_free) {
            total += current->size - sizeof(struct heap_header);
        }
        current = current->next;
    }
    return total;
}

extern "C" uint64_t heap_total_used() {
    uint64_t total = 0;
    struct heap_header* current = heap_start;
    while (current != 0) {
        if (!current->is_free) {
            total += current->size - sizeof(struct heap_header);
        }
        current = current->next;
    }
    return total;
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
