#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 메모리 블록의 정보를 담는 머리표(Header)
struct heap_header {
    uint32_t magic;
    uint32_t size;     // 블록의 크기 (헤더 포함)
    uint32_t requested_size;
    uint8_t  is_free;  // 1이면 비어있음, 0이면 사용 중
    uint8_t  in_free_list;
    uint8_t reserved[2];
    struct heap_header* next; // 다음 블록을 가리키는 포인터
    struct heap_header* prev; // 이전 블록을 가리키는 포인터
    struct heap_header* free_next;
    struct heap_header* free_prev;
};

typedef struct HeapStats {
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t mapped_bytes;
    uint64_t peak_used_bytes;
    uint64_t largest_free_bytes;
    uint64_t alloc_requests;
    uint64_t free_requests;
    uint64_t alloc_failures;
    uint64_t invalid_free_requests;
    uint64_t double_free_requests;
    uint64_t free_list_hits;
    uint64_t free_list_misses;
    uint32_t free_bin_count;
    uint32_t mapped_pages;
    uint32_t region_count;
    uint32_t grow_requests;
} HeapStats;

void heap_init();
void* kmalloc(size_t size);
void kfree(void* ptr);
void heap_coalesce();
uint64_t heap_total_free();
uint64_t heap_total_used();
uint64_t heap_total_mapped_bytes();
uint32_t heap_mapped_page_count();
uint32_t heap_region_count();
void heap_get_stats(HeapStats* out_stats);

extern struct heap_header* heap_start;

#ifdef __cplusplus
}
#endif

#endif
