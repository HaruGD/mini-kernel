#ifndef PAGING64_H
#define PAGING64_H

#include <stdint.h>

#define PAGING64_PAGE_SIZE 4096ULL
#define PAGING64_FLAG_PRESENT 0x001ULL
#define PAGING64_FLAG_WRITABLE 0x002ULL
#define PAGING64_FLAG_USER 0x004ULL
#define PAGING64_FLAG_WRITE_THROUGH 0x008ULL
#define PAGING64_FLAG_CACHE_DISABLE 0x010ULL
#define PAGING64_FLAG_ACCESSED 0x020ULL
#define PAGING64_FLAG_DIRTY 0x040ULL
#define PAGING64_FLAG_HUGE 0x080ULL
#define PAGING64_FLAG_GLOBAL 0x100ULL
#define PAGING64_FLAG_NX (1ULL << 63)

#define PAGING64_KERNEL_HEAP_BASE 0x0000000040000000ULL
#define PAGING64_KERNEL_HEAP_LIMIT 0x0000000060000000ULL

#ifdef __cplusplus
extern "C" {
#endif

void paging64_init();
void paging64_enable_nxe();
int paging64_is_nxe_enabled();
int paging64_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
int paging64_unmap_page(uint64_t virt);
int paging64_map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags);
int paging64_map_range_identity(uint64_t start, uint64_t size, uint64_t flags);
int paging64_remap_range(uint64_t virt, uint64_t size, uint64_t flags);
int paging64_alloc_map_range(uint64_t virt, uint64_t size, uint64_t flags, uint32_t* out_page_count);
uint32_t paging64_unmap_free_range(uint64_t virt, uint32_t page_count);
uint64_t paging64_get_phys(uint64_t virt);
uint64_t paging64_get_flags(uint64_t virt);
void paging64_flush_tlb(uint64_t virt);
uint64_t paging64_get_root_phys();

#ifdef __cplusplus
}
#endif

#endif
