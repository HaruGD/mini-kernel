#include <os64/os64.h>
#include "internal.h"

#define OS_HEAP_MAGIC 0x4F53484Du
#define OS_HEAP_ALIGNMENT 16u
#define OS_HEAP_MIN_SPLIT 16u

typedef struct OsHeapBlock {
    size_t size;
    struct OsHeapBlock* previous;
    struct OsHeapBlock* next;
    uint32_t magic;
    uint8_t free;
    uint8_t reserved[3];
} OsHeapBlock;

static OsHeapBlock* heap_head = 0;
static OsHeapBlock* heap_tail = 0;
static uint8_t* heap_break = 0;

static size_t align_size(size_t size) {
    if (size > SIZE_MAX - (OS_HEAP_ALIGNMENT - 1u)) {
        return 0;
    }
    return (size + OS_HEAP_ALIGNMENT - 1u) & ~(OS_HEAP_ALIGNMENT - 1u);
}

static int initialize_heap(void) {
    if (heap_break != 0) {
        return 1;
    }

    long result = os_syscall1(OS_SYS_BRK, 0);
    if (result < 0) {
        return 0;
    }
    heap_break = (uint8_t*)(uintptr_t)result;
    return 1;
}

void* os_brk(void* address) {
    long result = os_syscall1(OS_SYS_BRK, (long)address);
    if (result < 0) {
        return (void*)0;
    }
    heap_break = (uint8_t*)(uintptr_t)result;
    return heap_break;
}

static OsHeapBlock* find_free_block(size_t size) {
    OsHeapBlock* block = heap_head;
    while (block != 0) {
        if (block->magic == OS_HEAP_MAGIC && block->free && block->size >= size) {
            return block;
        }
        block = block->next;
    }
    return 0;
}

static void split_block(OsHeapBlock* block, size_t size) {
    if (block->size < size + sizeof(OsHeapBlock) + OS_HEAP_MIN_SPLIT) {
        return;
    }

    OsHeapBlock* remainder = (OsHeapBlock*)((uint8_t*)(block + 1) + size);
    remainder->size = block->size - size - sizeof(OsHeapBlock);
    remainder->previous = block;
    remainder->next = block->next;
    remainder->magic = OS_HEAP_MAGIC;
    remainder->free = 1;
    if (remainder->next != 0) {
        remainder->next->previous = remainder;
    } else {
        heap_tail = remainder;
    }
    block->next = remainder;
    block->size = size;
}

static OsHeapBlock* append_block(size_t size) {
    if (!initialize_heap() || size > SIZE_MAX - sizeof(OsHeapBlock)) {
        return 0;
    }

    size_t total_size = sizeof(OsHeapBlock) + size;
    uintptr_t old_break = (uintptr_t)heap_break;
    if (old_break > UINTPTR_MAX - total_size) {
        return 0;
    }
    if (os_brk((void*)(old_break + total_size)) == 0) {
        return 0;
    }

    OsHeapBlock* block = (OsHeapBlock*)old_break;
    block->size = size;
    block->previous = heap_tail;
    block->next = 0;
    block->magic = OS_HEAP_MAGIC;
    block->free = 0;
    if (heap_tail != 0) {
        heap_tail->next = block;
    } else {
        heap_head = block;
    }
    heap_tail = block;
    return block;
}

void* os_malloc(size_t size) {
    size_t aligned_size;
    OsHeapBlock* block;

    if (size == 0) {
        return 0;
    }
    aligned_size = align_size(size);
    if (aligned_size == 0) {
        return 0;
    }

    block = find_free_block(aligned_size);
    if (block != 0) {
        split_block(block, aligned_size);
        block->free = 0;
        return block + 1;
    }

    block = append_block(aligned_size);
    return block != 0 ? block + 1 : 0;
}

static OsHeapBlock* merge_with_next(OsHeapBlock* block) {
    OsHeapBlock* next = block->next;
    if (next == 0 || !next->free || next->magic != OS_HEAP_MAGIC) {
        return block;
    }

    block->size += sizeof(OsHeapBlock) + next->size;
    block->next = next->next;
    if (block->next != 0) {
        block->next->previous = block;
    } else {
        heap_tail = block;
    }
    return block;
}

void os_free(void* pointer) {
    if (pointer == 0) {
        return;
    }

    OsHeapBlock* block = ((OsHeapBlock*)pointer) - 1;
    if (block->magic != OS_HEAP_MAGIC || block->free) {
        return;
    }

    block->free = 1;
    block = merge_with_next(block);
    if (block->previous != 0 && block->previous->free) {
        block = merge_with_next(block->previous);
    }

    if (block == heap_tail) {
        OsHeapBlock* previous = block->previous;
        if (os_brk((void*)block) != 0) {
            heap_tail = previous;
            if (previous != 0) {
                previous->next = 0;
            } else {
                heap_head = 0;
            }
        }
    }
}

void* os_calloc(size_t count, size_t size) {
    if (count != 0 && size > SIZE_MAX / count) {
        return 0;
    }

    size_t total_size = count * size;
    void* pointer = os_malloc(total_size);
    if (pointer != 0) {
        os_memset(pointer, 0, total_size);
    }
    return pointer;
}

void* os_realloc(void* pointer, size_t size) {
    if (pointer == 0) {
        return os_malloc(size);
    }
    if (size == 0) {
        os_free(pointer);
        return 0;
    }

    size_t aligned_size = align_size(size);
    OsHeapBlock* block = ((OsHeapBlock*)pointer) - 1;
    if (aligned_size == 0 || block->magic != OS_HEAP_MAGIC || block->free) {
        return 0;
    }
    if (block->size >= aligned_size) {
        split_block(block, aligned_size);
        return pointer;
    }
    if (block->next != 0 && block->next->free &&
        block->size + sizeof(OsHeapBlock) + block->next->size >= aligned_size) {
        merge_with_next(block);
        split_block(block, aligned_size);
        block->free = 0;
        return pointer;
    }

    void* replacement = os_malloc(size);
    if (replacement == 0) {
        return 0;
    }
    os_memcpy(replacement, pointer, block->size);
    os_free(pointer);
    return replacement;
}

void* os_memset(void* destination, int value, size_t size) {
    uint8_t* output = (uint8_t*)destination;
    for (size_t i = 0; i < size; i++) {
        output[i] = (uint8_t)value;
    }
    return destination;
}

void* os_memcpy(void* destination, const void* source, size_t size) {
    uint8_t* output = (uint8_t*)destination;
    const uint8_t* input = (const uint8_t*)source;
    for (size_t i = 0; i < size; i++) {
        output[i] = input[i];
    }
    return destination;
}

char* os_strdup(const char* text) {
    if (text == 0) {
        return 0;
    }

    size_t length = os_strlen(text) + 1u;
    char* copy = (char*)os_malloc(length);
    if (copy != 0) {
        os_memcpy(copy, text, length);
    }
    return copy;
}
