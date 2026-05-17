#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 메모리 블록의 정보를 담는 머리표(Header)
struct heap_header {
    uint32_t size;     // 블록의 크기 (헤더 포함)
    uint8_t  is_free;  // 1이면 비어있음, 0이면 사용 중
    struct heap_header* next; // 다음 블록을 가리키는 포인터
};

void heap_init();
void* kmalloc(size_t size);
void kfree(void* ptr);
void heap_coalesce();
uint64_t heap_total_free();
uint64_t heap_total_used();
uint64_t heap_total_mapped_bytes();
uint32_t heap_mapped_page_count();
uint32_t heap_region_count();

extern struct heap_header* heap_start;

#ifdef __cplusplus
}
#endif

#endif
