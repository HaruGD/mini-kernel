#include "paging.h"
#include "drivers/terminal.hpp"

extern "C" {
    #include "pmm.h"
    #include "heap.h"
}

extern Terminal terminal;

Paging::Paging() : page_directory(0), page_tables(0) {}

void Paging::init() {
    // Page Directory 할당 (4KB 정렬 필요)
    page_directory = (uint32_t*)pmm_alloc_block();
    
    // Page Directory 초기화
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002;  // Not present, writable
    }

    // 커널 영역 1:1 매핑 (0x0 ~ 0x400000, 4MB)
    // 4MB = 1024 페이지
    uint32_t* page_table = (uint32_t*)pmm_alloc_block();
    
    for (int i = 0; i < 1024; i++) {
        page_table[i] = (i * 0x1000) | PAGE_PRESENT | PAGE_WRITABLE;
    }

    // 0번 디렉토리 엔트리에 page_table 연결
    page_directory[0] = (uint32_t)page_table | PAGE_PRESENT | PAGE_WRITABLE;

    // VGA 메모리 매핑 (0xB8000)
    // 0xB8000은 0번 테이블 안에 있으므로 이미 매핑됨
}

void Paging::enable() {
    // CR3에 Page Directory 주소 설정
    __asm__ volatile("mov %0, %%cr3" : : "r"(page_directory));

    // CR0의 PG 비트 (31번) 켜기
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

void Paging::map(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t dir_index   = virt >> 22;          // 상위 10비트
    uint32_t table_index = (virt >> 12) & 0x3FF; // 중간 10비트

    // 해당 디렉토리 엔트리가 없으면 새 페이지 테이블 만들기
    if (!(page_directory[dir_index] & PAGE_PRESENT)) {
        uint32_t* new_table = (uint32_t*)pmm_alloc_block();
        for (int i = 0; i < 1024; i++) new_table[i] = 0x00000002;
        page_directory[dir_index] = (uint32_t)new_table | PAGE_PRESENT | PAGE_WRITABLE;
    }

    uint32_t* table = (uint32_t*)(page_directory[dir_index] & 0xFFFFF000);
    table[table_index] = phys | flags;
}