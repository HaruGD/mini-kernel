#include "pmm.h"

// 메모리 상태를 기록할 비트맵 배열
uint8_t memory_bitmap[BITMAP_SIZE];

// 비트맵에서 특정 비트를 1로 설정 (사용 중)
static inline void mmap_set(int bit) {
    memory_bitmap[bit / 8] |= (1 << (bit % 8));
}

// 비트맵에서 특정 비트를 0으로 설정 (비어 있음)
static inline void mmap_unset(int bit) {
    memory_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

// 비트맵 초기화
void pmm_init() {
    // 1. 일단 모든 메모리를 '비어있음(0)'으로 설정
    for (int i = 0; i < BITMAP_SIZE; i++) {
        memory_bitmap[i] = 0;
    }

    // 2. 커널이 이미 차지하고 있는 영역(0MB ~ 1MB 등)은 '사용 중'으로 보호해야 합니다.
    // 일단 0~1MB(256페이지)는 예약석으로 설정합시다.
    for (int i = 0; i < 512; i++) {
        mmap_set(i);
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