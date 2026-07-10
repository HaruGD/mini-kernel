#ifndef KERNEL_MM_ARCH_VM_H
#define KERNEL_MM_ARCH_VM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void arch_vm_init();
void arch_vm_enable_execute_disable();
int arch_vm_is_execute_disable_enabled();
int arch_vm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
int arch_vm_unmap_page(uint64_t virt);
uint64_t arch_vm_get_phys(uint64_t virt);
uint64_t arch_vm_get_flags(uint64_t virt);
void arch_vm_flush_page(uint64_t virt);
uint64_t arch_vm_get_root_phys();
uint64_t arch_vm_create_root();
void arch_vm_switch_root(uint64_t root_phys);
int arch_vm_map_page_in_root(uint64_t root_phys, uint64_t virt, uint64_t phys, uint64_t flags);
int arch_vm_unmap_page_in_root(uint64_t root_phys, uint64_t virt);
uint64_t arch_vm_get_phys_in_root(uint64_t root_phys, uint64_t virt);
uint64_t arch_vm_get_flags_in_root(uint64_t root_phys, uint64_t virt);

#ifdef __cplusplus
}
#endif

#endif
