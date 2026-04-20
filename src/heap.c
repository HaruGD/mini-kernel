#include "heap.h"
#include "arch/x86/pmm.h"

void debug_print(const char* str);
void debug_print_hex(uint32_t val);

struct heap_header* heap_start = NULL;

void heap_init() {
    // 1. PMM에게 4KB 한 페이지를 빌려옵니다.
    heap_start = (struct heap_header*)pmm_alloc_block();

    // 2. 초기 상태: 4KB 전체가 하나의 비어있는 블록입니다.
    heap_start->size = 4096;
    heap_start->is_free = 1;
    heap_start->next = NULL;

    struct heap_header* current = heap_start;
    for (int i = 1; i < 16; i++) {
        struct heap_header* next = (struct heap_header*)pmm_alloc_block();
        next->size = 4096;
        next->is_free = 1;
        next->next = NULL;
        current->next = next;
        current = next;
    }
}

void* kmalloc(size_t size) {
    // 사용자가 요청한 크기에 헤더 크기를 더합니다.
    size_t total_size = size + sizeof(struct heap_header);
    if (total_size % 4 != 0) {
        total_size = (total_size / 4 + 1) * 4;
    }

    struct heap_header* current = heap_start;

    // 빈 공간 찾기 (First-Fit 방식)
    while (current != NULL) {
        if (current->is_free && current->size >= total_size) {
            debug_print("\nkmalloc: found at ");
            debug_print_hex((uint32_t)current);
            debug_print("\ntotal_size: ");
            debug_print_hex(total_size);

            // 딱 맞는 공간을 찾았다면 사용 중으로 표시!
            current->is_free = 0;

            // 만약 남은 공간이 넉넉하다면, 블록을 쪼개서(Split) 다음 블록을 만듭니다.
            if (current->size >= total_size + sizeof(struct heap_header) + 4) {
                debug_print("\nsplit: current->size=");
                debug_print_hex(current->size);
                debug_print(" total_size=");
                debug_print_hex(total_size);

                struct heap_header* next_block = (struct heap_header*)((uint32_t)current + total_size);
                next_block->size = current->size - total_size;
                debug_print("\nnext_block->size after write: ");
                debug_print_hex(next_block->size);
                next_block->is_free = 1;
                next_block->next = current->next;

                current->size = total_size;
                debug_print("\ncurrent->size after update: ");
                debug_print_hex(current->size);
                current->next = next_block;
            }

            // 실제 데이터가 시작되는 지점(헤더 바로 뒤)의 주소를 반환합니다.
            return (void*)((uint32_t)current + sizeof(struct heap_header));
        }
        current = current->next;
    }
    return NULL; // 남은 땅이 없음!
}

void heap_coalesce() {
    struct heap_header* curr = heap_start;
    int limit = 0;

    while (curr != NULL && curr->next != NULL && limit < 100) {
        if (curr->is_free && curr->next->is_free) {
            // 실제로 메모리상 연속된 블록인지 확인
            uint32_t curr_end = (uint32_t)curr + curr->size;
            uint32_t next_start = (uint32_t)curr->next;

            if (curr_end == next_start) {
                // 연속된 경우만 합치기
                curr->size += curr->next->size;
                curr->next = curr->next->next;
            } else {
                curr = curr->next;
            }
        } else {
            curr = curr->next;
        }
        limit++;
    }
}

void kfree(void* ptr) {
    struct heap_header* header =
        (struct heap_header*)((uint32_t)ptr - sizeof(struct heap_header));
    debug_print("\nkfree: header at ");
    debug_print_hex((uint32_t)header);
    debug_print(" size: ");
    debug_print_hex(header->size);
    header->is_free = 1;
    heap_coalesce();
}
