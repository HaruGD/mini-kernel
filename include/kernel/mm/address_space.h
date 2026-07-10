#ifndef KERNEL_MM_ADDRESS_SPACE_H
#define KERNEL_MM_ADDRESS_SPACE_H

#include <stdint.h>

#include "kernel/mm/vm.h"

#define ADDRESS_SPACE_MAX_REGIONS 16

#define ADDRESS_SPACE_REGION_READ    0x00000001U
#define ADDRESS_SPACE_REGION_WRITE   0x00000002U
#define ADDRESS_SPACE_REGION_EXECUTE 0x00000004U

struct AddressSpaceRegion {
    uint8_t active;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t rights;
    uint64_t start;
    uint64_t end;
};

struct AddressSpace {
    uint64_t root_phys;
    uint64_t code_base;
    uint64_t elf_link_base;
    uint64_t stack_guard_base;
    uint64_t stack_base;
    uint64_t heap_base;
    uint64_t heap_break;
    uint64_t heap_mapped_end;
    uint64_t heap_limit;
    uint32_t code_page_count;
    uint32_t elf_alias_page_count;
    uint32_t stack_guard_page_count;
    uint32_t stack_page_count;
    uint32_t heap_page_count;
    uint32_t region_count;
    AddressSpaceRegion regions[ADDRESS_SPACE_MAX_REGIONS];
};

void address_space_init(AddressSpace* space);
int address_space_ensure_root(AddressSpace* space);
void address_space_reset_user(AddressSpace* space);
void address_space_activate(const AddressSpace* space);
void address_space_activate_kernel();

int address_space_add_region(AddressSpace* space, uint64_t start, uint64_t size, uint32_t rights);
void address_space_remove_region(AddressSpace* space, uint64_t start, uint64_t size);
int address_space_owns_address(const AddressSpace* space, uint64_t address);
int address_space_buffer_accessible(const AddressSpace* space, uint64_t start, uint32_t size, int writable);

int address_space_map_page(AddressSpace* space, uint64_t virt, uint64_t phys, uint64_t flags);
int address_space_unmap_page(AddressSpace* space, uint64_t virt);
int address_space_protect_range(AddressSpace* space, uint64_t virt, uint64_t size, uint64_t flags);
int address_space_alloc_map_range(AddressSpace* space,
                                  uint64_t virt,
                                  uint64_t size,
                                  uint64_t flags,
                                  uint32_t* out_page_count);
uint32_t address_space_unmap_free_range(AddressSpace* space, uint64_t virt, uint32_t page_count);
uint64_t address_space_get_phys(const AddressSpace* space, uint64_t virt);
uint64_t address_space_get_flags(const AddressSpace* space, uint64_t virt);

#endif
