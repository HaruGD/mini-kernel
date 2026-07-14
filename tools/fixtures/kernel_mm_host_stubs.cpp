#include "kernel_mm_host_stubs.h"

#include <stddef.h>
#include <stdlib.h>

#include "kernel/fault_injection.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vm.h"

#define HOST_MM_MAX_PAGES 16384u

struct HostMapping {
    uint8_t active;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t reserved2;
    uint64_t virt;
    uint64_t phys;
    uint64_t flags;
};

static void* allocated_pages[HOST_MM_MAX_PAGES];
static HostMapping mappings[HOST_MM_MAX_PAGES];
static int32_t map_successes_before_failure = -1;
static int32_t unmap_successes_before_failure = -1;

void host_mm_reset() {
    for (uint32_t i = 0; i < HOST_MM_MAX_PAGES; i++) {
        if (allocated_pages[i] != 0) {
            free(allocated_pages[i]);
            allocated_pages[i] = 0;
        }
        mappings[i].active = 0;
        mappings[i].virt = 0;
        mappings[i].phys = 0;
        mappings[i].flags = 0;
    }
    map_successes_before_failure = -1;
    unmap_successes_before_failure = -1;
}

uint32_t host_mm_allocated_pages() {
    uint32_t count = 0;
    for (uint32_t i = 0; i < HOST_MM_MAX_PAGES; i++) {
        count += allocated_pages[i] != 0 ? 1u : 0u;
    }
    return count;
}

uint32_t host_mm_mapped_pages() {
    uint32_t count = 0;
    for (uint32_t i = 0; i < HOST_MM_MAX_PAGES; i++) {
        count += mappings[i].active ? 1u : 0u;
    }
    return count;
}

void host_mm_fail_map_after(int32_t successes_before_failure) {
    map_successes_before_failure = successes_before_failure;
}

void host_mm_fail_unmap_after(int32_t successes_before_failure) {
    unmap_successes_before_failure = successes_before_failure;
}

extern "C" void* pmm_alloc_block() {
    if (kernel_fault_injection_should_fail(KERNEL_FAULT_POINT_PMM)) {
        return 0;
    }

    uint32_t slot = HOST_MM_MAX_PAGES;
    for (uint32_t i = 0; i < HOST_MM_MAX_PAGES; i++) {
        if (allocated_pages[i] == 0) {
            slot = i;
            break;
        }
    }
    if (slot == HOST_MM_MAX_PAGES) {
        return 0;
    }

    void* page = 0;
    if (posix_memalign(&page, PMM_PAGE_SIZE, PMM_PAGE_SIZE) != 0) {
        return 0;
    }
    allocated_pages[slot] = page;
    return page;
}

extern "C" void pmm_free_block(void* address) {
    if (address == 0) {
        return;
    }
    for (uint32_t i = 0; i < HOST_MM_MAX_PAGES; i++) {
        if (allocated_pages[i] == address) {
            free(allocated_pages[i]);
            allocated_pages[i] = 0;
            return;
        }
    }
}

extern "C" int vm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (map_successes_before_failure == 0) {
        map_successes_before_failure = -1;
        return 0;
    }
    if (map_successes_before_failure > 0) {
        map_successes_before_failure--;
    }

    for (uint32_t i = 0; i < HOST_MM_MAX_PAGES; i++) {
        if (mappings[i].active && mappings[i].virt == virt) {
            mappings[i].phys = phys;
            mappings[i].flags = flags;
            return 1;
        }
    }
    for (uint32_t i = 0; i < HOST_MM_MAX_PAGES; i++) {
        if (!mappings[i].active) {
            mappings[i].active = 1;
            mappings[i].virt = virt;
            mappings[i].phys = phys;
            mappings[i].flags = flags;
            return 1;
        }
    }
    return 0;
}

extern "C" int vm_unmap_page(uint64_t virt) {
    if (unmap_successes_before_failure == 0) {
        unmap_successes_before_failure = -1;
        return 0;
    }
    if (unmap_successes_before_failure > 0) {
        unmap_successes_before_failure--;
    }
    for (uint32_t i = 0; i < HOST_MM_MAX_PAGES; i++) {
        if (mappings[i].active && mappings[i].virt == virt) {
            mappings[i].active = 0;
            mappings[i].virt = 0;
            mappings[i].phys = 0;
            mappings[i].flags = 0;
            return 1;
        }
    }
    return 0;
}

extern "C" uint64_t vm_get_phys(uint64_t virt) {
    for (uint32_t i = 0; i < HOST_MM_MAX_PAGES; i++) {
        if (mappings[i].active && mappings[i].virt == (virt & ~(VM_PAGE_SIZE - 1u))) {
            return mappings[i].phys | (virt & (VM_PAGE_SIZE - 1u));
        }
    }
    return 0;
}

extern "C" uint64_t vm_get_flags(uint64_t virt) {
    uint64_t page = virt & ~(VM_PAGE_SIZE - 1u);
    for (uint32_t i = 0; i < HOST_MM_MAX_PAGES; i++) {
        if (mappings[i].active && mappings[i].virt == page) {
            return mappings[i].flags | VM_FLAG_PRESENT;
        }
    }
    return 0;
}

extern "C" uint64_t vm_get_root_phys() {
    return 0x1000u;
}

extern "C" uint64_t vm_create_root() {
    static uint64_t next_root = 0x2000u;
    uint64_t root = next_root;
    next_root += VM_PAGE_SIZE;
    return root;
}

extern "C" void vm_switch_root(uint64_t) {
}

extern "C" int vm_map_page_in_root(uint64_t, uint64_t virt, uint64_t phys, uint64_t flags) {
    return vm_map_page(virt, phys, flags);
}

extern "C" int vm_unmap_page_in_root(uint64_t, uint64_t virt) {
    return vm_unmap_page(virt);
}

extern "C" uint64_t vm_get_phys_in_root(uint64_t, uint64_t virt) {
    return vm_get_phys(virt);
}

extern "C" uint64_t vm_get_flags_in_root(uint64_t, uint64_t virt) {
    return vm_get_flags(virt);
}

extern "C" int vm_protect_range_in_root(uint64_t,
                                          uint64_t virt,
                                          uint64_t size,
                                          uint64_t flags) {
    for (uint64_t offset = 0; offset < size; offset += VM_PAGE_SIZE) {
        uint64_t phys = vm_get_phys(virt + offset);
        if (phys == 0 || !vm_map_page(virt + offset,
                                     phys & ~(VM_PAGE_SIZE - 1u),
                                     flags)) {
            return 0;
        }
    }
    return 1;
}

extern "C" int vm_alloc_map_range_in_root(uint64_t,
                                            uint64_t,
                                            uint64_t,
                                            uint64_t,
                                            uint32_t*) {
    return 0;
}

extern "C" uint32_t vm_unmap_free_range_in_root(uint64_t,
                                                  uint64_t virt,
                                                  uint32_t page_count) {
    uint32_t unmapped = 0;
    for (uint32_t i = 0; i < page_count; i++) {
        uint64_t address = virt + (uint64_t)i * VM_PAGE_SIZE;
        uint64_t phys = vm_get_phys(address) & ~(VM_PAGE_SIZE - 1u);
        if (phys != 0 && vm_unmap_page(address)) {
            pmm_free_block((void*)(uintptr_t)phys);
            unmapped++;
        }
    }
    return unmapped;
}
