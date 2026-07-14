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
