#include "heap.h"
#include "arch/x86/pmm64.h"

static struct heap_header* heap_tail = 0;

static uintptr_t align_up_heap(uintptr_t value, uintptr_t align) {
    return (value + align - 1) & ~(align - 1);
}

struct heap_header* heap_start = 0;

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
    uintptr_t region = (uintptr_t)pmm64_alloc_blocks(page_count);
    if (region == 0) {
        return 0;
    }

    struct heap_header* block = (struct heap_header*)region;
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

extern "C" void heap_init() {
    heap_start = 0;
    heap_tail = 0;
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
