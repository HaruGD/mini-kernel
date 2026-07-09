#ifndef KERNEL_MM_VM_H
#define KERNEL_MM_VM_H

#include <stdint.h>

#define VM_PAGE_SIZE 4096ULL
#define VM_FLAG_PRESENT 0x001ULL
#define VM_FLAG_WRITABLE 0x002ULL
#define VM_FLAG_USER 0x004ULL
#define VM_FLAG_WRITE_THROUGH 0x008ULL
#define VM_FLAG_CACHE_DISABLE 0x010ULL
#define VM_FLAG_ACCESSED 0x020ULL
#define VM_FLAG_DIRTY 0x040ULL
#define VM_FLAG_HUGE 0x080ULL
#define VM_FLAG_GLOBAL 0x100ULL
#define VM_FLAG_NO_EXECUTE (1ULL << 63)

#define VM_KERNEL_HEAP_BASE 0x0000000040000000ULL
#define VM_KERNEL_HEAP_LIMIT 0x0000000060000000ULL

#ifdef __cplusplus
extern "C" {
#endif

void vm_init();
void vm_enable_execute_disable();
int vm_is_execute_disable_enabled();
int vm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
int vm_unmap_page(uint64_t virt);
int vm_map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags);
int vm_map_identity(uint64_t start, uint64_t size, uint64_t flags);
int vm_protect_range(uint64_t virt, uint64_t size, uint64_t flags);
int vm_alloc_map_range(uint64_t virt, uint64_t size, uint64_t flags, uint32_t* out_page_count);
uint32_t vm_unmap_free_range(uint64_t virt, uint32_t page_count);
uint64_t vm_get_phys(uint64_t virt);
uint64_t vm_get_flags(uint64_t virt);
void vm_flush_page(uint64_t virt);
uint64_t vm_get_root_phys();

#ifdef __cplusplus
}
#endif

#endif
