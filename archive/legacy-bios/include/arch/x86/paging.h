#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include "drivers/driver.h"

#define PAGE_PRESENT    0x1
#define PAGE_WRITABLE   0x2
#define PAGE_USER       0x4

class Paging {
    uint32_t* page_directory;
    uint32_t* page_tables;

    void map_page(uint32_t virt, uint32_t phys, uint32_t flags);

public:
    Paging();
    void init();
    void enable();
    void map(uint32_t virt, uint32_t phys, uint32_t flags);
};

#endif